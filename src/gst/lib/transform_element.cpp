/*******************************************************************************
 * Copyright (C) 2022-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "dlstreamer/base/transform.h"
#include "dlstreamer/gst/context.h"
#include "dlstreamer/gst/frame_batch.h"
#include "dlstreamer/gst/mappers/any_to_gst.h"
#include "dlstreamer/gst/metadata/gva_tensor_meta.h"
#include "dlstreamer/gst/plugin.h"
#include "dlstreamer/gst/utils.h"
#include "dlstreamer/image_metadata.h"
#include "gst_logger_sink.h"
#include <spdlog/spdlog.h>

#include "shared_instance.h"

#define CATCH_EXCEPTIONS

#ifndef ITT_TASK
#define ITT_TASK(NAME)
#endif

GST_DEBUG_CATEGORY_STATIC(gva_transform_element_debug);
#define GST_CAT_DEFAULT gva_transform_element_debug

namespace dlstreamer {

template <auto>
struct Callback;

template <class T, class Ret, class... Args, Ret (T::*mem_fn)(Args...)>
struct Callback<mem_fn> {
    static Ret fn(GObject *obj, Args... args) {
        return (T::unpack(obj)->*mem_fn)(std::forward<Args>(args)...);
    }

    static Ret fn(GstBaseTransform *obj, Args... args) {
        return (T::unpack(obj)->*mem_fn)(std::forward<Args>(args)...);
    }
};

struct GstDlsTransformClass {
    GstBaseTransformClass base_class;

    GstCaps *(*default_transform_caps)(GstBaseTransform *trans, GstPadDirection direction, GstCaps *caps,
                                       GstCaps *filter);
    GstFlowReturn (*default_generate_output)(GstBaseTransform *trans, GstBuffer **outbuf);
    ElementDesc *desc;
    gint private_offset;

    static void init(gpointer g_class, gpointer class_data);

    static void finalize(gpointer g_class, gpointer /*class_data*/) {
        auto *self = static_cast<GstDlsTransformClass *>(g_class);
        if (self->enums_storage)
            gst_structure_free(self->enums_storage);
    }

    GstStructure *enums_storage;
};

class GstDlsTransform {
  public:
    struct GstPodData {
        GstBaseTransform base;
        GstDlsTransform *instance;
    };

    static GstDlsTransform *unpack(GstBaseTransform *base) {
        assert(reinterpret_cast<GstPodData *>(base)->instance);
        return reinterpret_cast<GstPodData *>(base)->instance;
    }

    static GstDlsTransform *unpack(GObject *object) {
        return unpack(GST_BASE_TRANSFORM(object));
    }

    static void instance_init(GTypeInstance *instance, gpointer g_class) {
        auto *self = reinterpret_cast<GstPodData *>(instance);

        gpointer instance_memory = g_type_instance_get_private(instance, G_TYPE_FROM_INSTANCE(instance));
        // This won't be converted to shared ptr because of memory placement
        self->instance = new (instance_memory) GstDlsTransform(&self->base, g_class);

        // Set chain_list function to process GstBuffer batch passes as buffer list
        // if (self->instance->_class_data->desc->flags & ELEMENT_FLAG_BATCH_INPUT)
        {
            gst_pad_set_chain_list_function(self->instance->_base->sinkpad,
                                            [](GstPad * /*pad*/, GstObject *parent, GstBufferList *list) {
                                                auto *self = reinterpret_cast<GstPodData *>(parent);
                                                return self->instance->transform_list(list);
                                            });
        }
    }

    static void instance_finalize(GObject *object) {
        auto self = reinterpret_cast<GstPodData *>(object);
        assert(self->instance);

        auto parent_class = self->instance->_parent_class;
        // Manually invoke object destruction since it was created via placement-new.
        self->instance->~GstDlsTransform();
        self->instance = nullptr;

        G_OBJECT_CLASS(parent_class)->finalize(object);
    }

    GstDlsTransform(GstBaseTransform *base, gpointer g_class)
        : _base(base), _class_data(static_cast<GstDlsTransformClass *>(g_class)) {
        _parent_class = GST_BASE_TRANSFORM_CLASS(g_type_class_peek_parent(_class_data));
        _logger = log::init_logger(GST_CAT_DEFAULT, nullptr);
        _gst_context = std::make_shared<GSTContext>(&base->element);
#if !(_WIN32)
        _gst_mapper = std::make_shared<MemoryMapperAnyToGST>(nullptr, _gst_context);
#endif
        //_gst_mapper = std::make_shared<MemoryMapperCache>(_gst_mapper);
        if (_class_data->desc->params) {
            for (auto const &param : *_class_data->desc->params) {
                _params->set(param.name, param.default_value);
            }
        }
    }

    ~GstDlsTransform() {
        BaseTransform *base_tran = dynamic_cast<BaseTransform *>(_transform);
        if (base_tran)
            GST_WARNING("%s: frame pool size on deletion = %ld", _base->element.object.name, base_tran->pool_size());
        SharedInstance::global()->clean_up();
    }

    void get_property(guint property_id, GValue *value, GParamSpec *pspec);
    void set_property(guint property_id, const GValue *value, GParamSpec *pspec);
    gboolean query(GstPadDirection direction, GstQuery *query);

    gboolean start() {
        GST_DEBUG_OBJECT(_base, "start");
        create_instance();
        return (_element != nullptr);
    }

    gboolean create_instance() {
        if (_element)
            return true;

#ifdef CATCH_EXCEPTIONS
        try {
#else
        {
#endif
            _logger = log::init_logger(GST_CAT_DEFAULT, G_OBJECT(_base));
            _params->set(dlstreamer::param::logger_name, _logger->name());
            _element = ElementPtr(_class_data->desc->create(_params, _gst_context));
            if (!_element) {
                GST_ELEMENT_ERROR(_base, LIBRARY, INIT, ("Invalid create function"),
                                  ("The create function returned null"));
            }
#ifdef CATCH_EXCEPTIONS
        } catch (std::exception &e) {
            GST_ELEMENT_ERROR(_base, LIBRARY, INIT, ("Couldn't create instance"),
                              ("The create function threw an exception: %s", e.what()));
        }
#else
        }
#endif
        if (!_element)
            return false;

        _transform = dynamic_cast<Transform *>(_element.get());
        _transform_inplace = dynamic_cast<TransformInplace *>(_element.get());
        assert(_transform || _transform_inplace);

        gst_base_transform_set_in_place(_base, _transform_inplace ? true : false);

        return true;
    }

    gboolean set_caps(GstCaps *incaps, GstCaps *outcaps);
    GstCaps *transform_caps(GstPadDirection direction, GstCaps *caps, GstCaps *filter);
    GstFlowReturn generate_output(GstBuffer **outbuf);
    GstFlowReturn transform(GstBuffer *inbuf, GstBuffer *outbuf);
    GstFlowReturn transform_ip(GstBuffer *buf);
    gboolean sink_event(GstEvent *event);
    GstFlowReturn transform_list(GstBufferList *list);

  private:
    void init_transform() {
        if (_transform_initialized)
            return;

        auto init_function = [this]() {
            if (_transform) {
                log_frame_info(GST_LEVEL_INFO, "INIT input_info", _input_info);
                log_frame_info(GST_LEVEL_INFO, "INIT output_info", _output_info);
                _transform->set_input_info(_input_info);
                _transform->set_output_info(_output_info);
                _transform->init();
            } else if (_transform_inplace) {
                log_frame_info(GST_LEVEL_INFO, "INIT info", _input_info);
                _transform_inplace->set_info(_input_info);
                _transform_inplace->init();
            } else {
                throw std::runtime_error("Transform not set");
            }
        };

        if (!_shared_instance_id.empty()) {
            auto params = dynamic_cast<BaseDictionary *>(_params.get());
            if (!params)
                throw std::runtime_error("Params are NULL");
            SharedInstance::InstanceId id = {std::string(_class_data->desc->name), _shared_instance_id, *params,
                                             _input_info, _output_info};
            _element = SharedInstance::global()->init_or_reuse(id, _element, init_function);
            _transform = dynamic_cast<Transform *>(_element.get());
            _transform_inplace = dynamic_cast<TransformInplace *>(_element.get());
        } else {
            init_function();
        }

        _transform_initialized = true;
    }

    void log_frame_info(GstDebugLevel level, std::string_view msg, const FrameInfo &info) {
        if (level <= _gst_debug_min) {
            auto str = frame_info_to_string(info);
            gst_debug_log(GST_CAT_DEFAULT, level, "", "", 0, G_OBJECT(_base), "%s: %s", msg.data(), str.data());
        }
    }

    void log_frame_infos(GstDebugLevel level, const std::string &msg, const FrameInfoVector &infos) {
        if (level <= _gst_debug_min) {
            std::string str;
            for (auto info = infos.begin(); info != infos.end(); info++) {
                if (info != infos.begin())
                    str += "; ";
                str += frame_info_to_string(*info).data();
            }
            gst_debug_log(GST_CAT_DEFAULT, level, "", "", 0, G_OBJECT(_base), "%s: %s", msg.data(), str.data());
        }
    }

  private:
    GstBaseTransform *_base;
    GstDlsTransformClass *_class_data;
    GstBaseTransformClass *_parent_class;
    ContextPtr _gst_context;

    ElementPtr _element;
    Transform *_transform = nullptr;
    TransformInplace *_transform_inplace = nullptr;
    bool _transform_initialized = false;
    std::mutex _mutex;

    DictionaryPtr _params = std::make_shared<BaseDictionary>();

    std::string _shared_instance_id;
    MemoryMapperPtr _gst_mapper;
    FrameInfo _input_info;
    FrameInfo _output_info;
    GstVideoInfo _input_video_info = {};
    GstVideoInfo _output_video_info = {};

    std::shared_ptr<spdlog::logger> _logger;
};

void GstDlsTransform::get_property(guint property_id, GValue *value, GParamSpec *pspec) {
    const ParamDescVector &params_desc = _class_data->desc->params ? *_class_data->desc->params : ParamDescVector{};

    if (property_id == 0 || property_id > params_desc.size()) {
        // Test for additional properties related added by GstDlsTransform implementation
        if (std::string_view(pspec->name) == std::string_view(param::shared_instance_id)) {
            g_value_set_string(value, _shared_instance_id.c_str());
            return;
        }
        G_OBJECT_WARN_INVALID_PROPERTY_ID(_base, property_id, pspec);
        return;
    }

    const ParamDesc &desc = params_desc.at(property_id - 1);
    auto opt_val = _params->try_get(desc.name);
    if (!opt_val)
        opt_val = desc.default_value;
    any_to_gvalue(*opt_val, value, false, &desc);
}

void GstDlsTransform::set_property(guint property_id, const GValue *value, GParamSpec *pspec) {
    const ParamDescVector &params_desc = _class_data->desc->params ? *_class_data->desc->params : ParamDescVector{};

    if (property_id == 0 || property_id > params_desc.size()) {
        // Test for additional properties related added by GstDlsTransform implementation
        if (std::string_view(pspec->name) == std::string_view(param::shared_instance_id)) {
            _shared_instance_id = g_value_get_string(value);
            return;
        }
        G_OBJECT_WARN_INVALID_PROPERTY_ID(_base, property_id, pspec);
        return;
    }

    try {
        auto &desc = params_desc.at(property_id - 1);
        Any val = *gvalue_to_any(value, &desc);
        _params->set(desc.name, val);
    } catch (...) {
        G_OBJECT_WARN_INVALID_PROPERTY_ID(_base, property_id, pspec);
    }
}

gboolean GstDlsTransform::query(GstPadDirection direction, GstQuery *query) {
    GST_DEBUG_OBJECT(_base, "query");

    if (GST_QUERY_TYPE(query) == GST_QUERY_CONTEXT) {
        const gchar *context_type;
        gst_query_parse_context_type(query, &context_type);

        MemoryType memory_type = MemoryType::Any;
        try {
            memory_type = memory_type_from_string(context_type);
        } catch (...) {
        }

        if (memory_type != MemoryType::Any && _element) {
            ContextPtr ctx = _element->get_context(memory_type);
            auto base_ctx = std::dynamic_pointer_cast<BaseContext>(ctx);
            if (ctx) {
                GstContext *gst_ctx = gst_context_new(context_type, FALSE);
                GstStructure *s = gst_context_writable_structure(gst_ctx);
                // TODO life-time of ContextPtr object
                for (const std::string &key : base_ctx->keys()) {
                    gst_structure_set(s, key.c_str(), G_TYPE_POINTER, ctx->handle(key), NULL);
                }
                gst_query_set_context(query, gst_ctx);
                gst_context_unref(gst_ctx);

                GST_LOG_OBJECT(_base, "Created context of type %s", context_type);
                return TRUE;
            }
        }
    }

    return _parent_class->query(_base, direction, query);
}

GstCaps *GstDlsTransform::transform_caps(GstPadDirection direction, GstCaps *caps, GstCaps *filter) {
    std::lock_guard<std::mutex> guard(_mutex);
    GST_DEBUG_OBJECT(_base, "transform_caps");

    if (!create_instance())
        return gst_caps_new_empty();

    if (!_transform && _transform_inplace) // if transform_inplace, call default function from BaseTransform
        return _class_data->default_transform_caps(_base, direction, caps, filter);

    GstCaps *ret_caps;
    if (_transform_initialized) { // if transform already initialized, we don't support changing caps
        const FrameInfo &info = (direction == GST_PAD_SRC) ? _input_info : _output_info;
        log_frame_info(GST_LEVEL_INFO, "get_info (after initialized)", info);
        ret_caps = frame_info_to_gst_caps(info);
    } else {
        ret_caps = gst_caps_new_empty();
        for (guint i = 0; i < gst_caps_get_size(caps); i++) {
            gint framerate_n = 0, framerate_d = 0;
            gst_structure_get_fraction(gst_caps_get_structure(caps, i), "framerate", &framerate_n, &framerate_d);

            FrameInfo info1 = gst_caps_to_frame_info(caps, i);
            FrameInfoVector info2vector;

            if (_transform) {
                if (direction == GST_PAD_SRC) {
                    log_frame_info(GST_LEVEL_INFO, "set_output_info", info1);
                    _transform->set_output_info(info1);
                    info2vector = _transform->get_input_info();
                    log_frame_infos(GST_LEVEL_INFO, "get_input_info", info2vector);
                } else {
                    log_frame_info(GST_LEVEL_INFO, "set_input_info", info1);
                    _transform->set_input_info(info1);
                    info2vector = _transform->get_output_info();
                    log_frame_infos(GST_LEVEL_INFO, "get_output_info", info2vector);
                }
            }

            if (!info2vector.empty()) {
                GstCaps *caps2 = frame_info_vector_to_gst_caps(info2vector);
                if (framerate_n && framerate_d) {
                    for (guint j = 0; j < gst_caps_get_size(caps2); j++) {
                        GstStructure *caps2_str = gst_caps_get_structure(caps2, j);
                        gst_structure_set(caps2_str, "framerate", GST_TYPE_FRACTION, framerate_n, framerate_d, NULL);
                    }
                }
                gst_caps_append(ret_caps, caps2);
            }
        }
    }
    if (filter) {
        GstCaps *intersection = gst_caps_intersect_full(filter, ret_caps, GST_CAPS_INTERSECT_FIRST);
        gst_caps_unref(ret_caps);
        ret_caps = intersection;
    }
    return ret_caps;
}

gboolean GstDlsTransform::set_caps(GstCaps *incaps, GstCaps *outcaps) {

    std::lock_guard<std::mutex> guard(_mutex);
    GST_DEBUG_OBJECT(_base, "set_caps");

    _input_info = gst_caps_to_frame_info(incaps);
    _output_info = gst_caps_to_frame_info(outcaps);
    if (_input_info.media_type == MediaType::Image)
        gst_video_info_from_caps(&_input_video_info, incaps);
    if (_output_info.media_type == MediaType::Image)
        gst_video_info_from_caps(&_output_video_info, outcaps);

#ifdef CATCH_EXCEPTIONS
    try {
        init_transform();
    } catch (std::exception &e) {
        GST_ELEMENT_ERROR(_base, CORE, FAILED, ("Couldn't prepare transform instance for processing"),
                          ("%s", e.what()));
    }
#else
    init_transform();
#endif
    return _transform_initialized;
}

static GstFramePtr gst_buffer_to_frame(GstBuffer *buffer, const FrameInfo &info, const GstVideoInfo *video_info,
                                       bool take_ownership, ContextPtr context) {
    if (video_info && video_info->finfo)
        return std::make_shared<GSTFrame>(buffer, video_info, nullptr, take_ownership, context);
    else
        return std::make_shared<GSTFrame>(buffer, info, take_ownership, context);
}

GstFlowReturn GstDlsTransform::generate_output(GstBuffer **outbuf) {
    if (!_transform || (_class_data->desc->flags & ELEMENT_FLAG_EXTERNAL_MEMORY)) {
        return _class_data->default_generate_output(_base, outbuf);
    }
    ITT_TASK(trans->element.object.name);
    GST_DEBUG_OBJECT(_base, "generate_output");

    if (!_base->queued_buf)
        return GST_FLOW_OK;

#ifdef CATCH_EXCEPTIONS
    try {
#endif
        GstBuffer *input = _base->queued_buf;
        GstFramePtr in = gst_buffer_to_frame(input, _input_info, &_input_video_info, true, _gst_context);
        _base->queued_buf = nullptr; // GSTFrame took ownership

        FramePtr out = _transform->process(in);

        if (!out) { // send gap event?
            // auto gap_event = gst_event_new_gap(GST_BUFFER_PTS(input), GST_BUFFER_DURATION(input));
            // if (!gst_pad_push_event(_base->srcpad, gap_event))
            //     return GST_FLOW_ERROR;
            return GST_BASE_TRANSFORM_FLOW_DROPPED;
        } else if (out == in) {
            *outbuf = gst_buffer_ref(input);
        } else {
            // map Frame to GSTFrame and get GSTFrame
            *outbuf = ptr_cast<GSTFrame>(_gst_mapper->map(out))->gst_buffer();
            // copy timestamps and metadata
            DLS_CHECK(gst_buffer_copy_into(*outbuf, input, GST_BUFFER_COPY_METADATA, 0, static_cast<gsize>(-1)))
        }

        return GST_FLOW_OK;
#ifdef CATCH_EXCEPTIONS
    } catch (const std::exception &e) {
        GST_ELEMENT_ERROR(_base, CORE, FAILED, ("Failed to process buffer"), ("%s", e.what()));
        return GST_FLOW_ERROR;
    }
#endif
}

GstFlowReturn GstDlsTransform::transform(GstBuffer *inbuf, GstBuffer *outbuf) {
    ITT_TASK(trans->element.object.name);
    GST_DEBUG_OBJECT(_base, "transform");

    try {
        GstFramePtr in = gst_buffer_to_frame(inbuf, _input_info, &_input_video_info, false, _gst_context);
        GstFramePtr out = gst_buffer_to_frame(outbuf, _output_info, &_output_video_info, false, _gst_context);
        _transform->process(in, out);

        // Copy timestamps and metadata
        DLS_CHECK(gst_buffer_copy_into(outbuf, inbuf, GST_BUFFER_COPY_METADATA, 0, static_cast<gsize>(-1)))

        return GST_FLOW_OK;
    } catch (const std::exception &e) {
        GST_ELEMENT_ERROR(_base, CORE, FAILED, ("Failed to process buffer"),
                          ("Buffer: %p. Error: %s", inbuf, e.what()));
        return GST_FLOW_ERROR;
    }
}

GstFlowReturn GstDlsTransform::transform_ip(GstBuffer *buf) {
    ITT_TASK(trans->element.object.name);
    GST_DEBUG_OBJECT(_base, "transform_ip");

#ifdef CATCH_EXCEPTIONS
    try {
#else
    {
#endif
        GstFramePtr transformed_frame = gst_buffer_to_frame(buf, _input_info, &_input_video_info, false, _gst_context);

        // TODO: return value means if we should drop buffer (send GAP) or not
        // May be introduce another method to check if buffer should be dropped, or by transform flag
        bool accepted = _transform_inplace->process(transformed_frame);

        if (!accepted) {
            GST_DEBUG_OBJECT(_base, "Push GAP event: ts=%" GST_TIME_FORMAT, GST_TIME_ARGS(GST_BUFFER_PTS(buf)));
            GstEvent *gap_event = gst_event_new_gap(GST_BUFFER_PTS(buf), GST_BUFFER_DURATION(buf));
            // If SourceIdentifierMetadata attached, copy all fields to GAP event
            auto source_id_meta = find_metadata(*transformed_frame, SourceIdentifierMetadata::name);
            if (source_id_meta) {
                GSTDictionary event_dict(gst_event_writable_structure(gap_event));
                copy_dictionary(*source_id_meta, event_dict);
            }
            // Push GAP event
            if (!gst_pad_push_event(_base->srcpad, gap_event)) {
                GST_ERROR_OBJECT(_base, "Failed to push GAP event buf: %p pts: %ld", buf, GST_BUFFER_PTS(buf));
                return GST_FLOW_ERROR;
            }
            return GST_BASE_TRANSFORM_FLOW_DROPPED;
        }
        return GST_FLOW_OK;
#ifdef CATCH_EXCEPTIONS
    } catch (const std::exception &e) {
        GST_ELEMENT_ERROR(_base, CORE, FAILED, ("Failed to process buffer inplace"),
                          ("Buffer: %p. PTS: %ld. Error: %s", buf, GST_BUFFER_PTS(buf), e.what()));
        return GST_FLOW_ERROR;
    }
#else
    }
#endif
}

GstFlowReturn GstDlsTransform::transform_list(GstBufferList *list) {
    GST_DEBUG_OBJECT(_base, "transform_list");

    try {
        // Wrap GstBufferList into dlstreamer::FramePtr
        DLS_CHECK(_input_video_info.finfo);
        FrameInfo info = gst_video_info_to_frame_info(&_input_video_info);
        GstFramePtr in = std::make_shared<GSTFrameBatch>(list, info, true, _gst_context);

        // Run processing on FramePtr containing list of tensors
        FramePtr out = _transform->process(in);

        if (!out) { // send gap event?
            return GST_FLOW_OK;
        }

        // map Frame to GSTFrame and get GstBuffer
        GstBuffer *outbuf = ptr_cast<GSTFrame>(_gst_mapper->map(out, AccessMode::ReadWrite))->gst_buffer();

        // Attach SourceIdentifierMetadata per each input buffer with PTS/stream_id/batch_index info
        GQuark stream_id_quark = g_quark_from_string(SourceIdentifierMetadata::key::stream_id);
        for (guint i = 0; i < gst_buffer_list_length(list); i++) {
            auto src = gst_buffer_list_get(list, i);
            auto src_meta = GST_GVA_TENSOR_META_GET(src);
            auto dst_meta = GST_GVA_TENSOR_META_ADD(outbuf);
            if (src_meta) {
                gst_structure_free(dst_meta->data);
                dst_meta->data = gst_structure_copy(src_meta->data);
            } else {
                gst_structure_set_name(dst_meta->data, SourceIdentifierMetadata::name);
            }
            void *stream_id = gst_mini_object_get_qdata(&src->mini_object, stream_id_quark); // attached by batch_create
            gst_structure_set(dst_meta->data, SourceIdentifierMetadata::key::stream_id, G_TYPE_POINTER, stream_id,
                              SourceIdentifierMetadata::key::batch_index, G_TYPE_INT, static_cast<int>(i),
                              SourceIdentifierMetadata::key::pts, G_TYPE_POINTER,
                              static_cast<intptr_t>(GST_BUFFER_PTS(src)), NULL);
        }

        // Push downstream
        return gst_pad_push(_base->srcpad, outbuf);
    } catch (const std::exception &e) {
        GST_ELEMENT_ERROR(_base, CORE, FAILED, ("Failed to process buffer list"),
                          ("List: %p. Error: %s", list, e.what()));
        return GST_FLOW_ERROR;
    }
}

/////////////////////////////////////////////////////////////////////////////////////

void GstDlsTransformClass::init(gpointer g_class, gpointer class_data) {
    GST_DEBUG_CATEGORY_INIT(gva_transform_element_debug, "gvatransformelement", 0,
                            "debug category for transform element");
    auto *self = static_cast<GstDlsTransformClass *>(g_class);
    auto *desc = static_cast<ElementDesc *>(class_data);
    self->desc = desc;

    self->private_offset = g_type_add_instance_private(G_TYPE_FROM_CLASS(g_class), sizeof(GstDlsTransform));
    g_type_class_adjust_private_offset(g_class, &self->private_offset);

    GstElementClass *element_class = GST_ELEMENT_CLASS(g_class);

    // input caps
    GstCaps *input_caps = frame_info_vector_to_gst_caps(desc->input_info());
    GstPadTemplate *sink_template = gst_pad_template_new("sink", GST_PAD_SINK, GST_PAD_ALWAYS, input_caps);
    gst_element_class_add_pad_template(element_class, sink_template);

    // output caps
    GstCaps *output_caps = frame_info_vector_to_gst_caps(desc->output_info());
    GstPadTemplate *src_template = gst_pad_template_new("src", GST_PAD_SRC, GST_PAD_ALWAYS, output_caps);
    gst_element_class_add_pad_template(element_class, src_template);

    // FIXME: The "Video" classification is not correct for all our use-cases
    gst_element_class_set_static_metadata(element_class, desc->description.data(), "Video", desc->description.data(),
                                          desc->author.data());

    GObjectClass *gobject_class = G_OBJECT_CLASS(g_class);
    gobject_class->finalize = GstDlsTransform::instance_finalize;
    gobject_class->set_property = Callback<&GstDlsTransform::set_property>::fn;
    gobject_class->get_property = Callback<&GstDlsTransform::get_property>::fn;

    GstBaseTransformClass *base_transform_class = GST_BASE_TRANSFORM_CLASS(g_class);
    self->default_generate_output = base_transform_class->generate_output;
    self->default_transform_caps = base_transform_class->transform_caps;

    base_transform_class->start = Callback<&GstDlsTransform::start>::fn;
    base_transform_class->set_caps = Callback<&GstDlsTransform::set_caps>::fn;
    base_transform_class->transform_caps = Callback<&GstDlsTransform::transform_caps>::fn;
    base_transform_class->query = Callback<&GstDlsTransform::query>::fn;
    base_transform_class->generate_output = Callback<&GstDlsTransform::generate_output>::fn;
    base_transform_class->transform_ip = Callback<&GstDlsTransform::transform_ip>::fn;
    base_transform_class->transform = Callback<&GstDlsTransform::transform>::fn;

    guint property_id = 0;
    if (desc->params) {
        self->enums_storage = gst_structure_new_empty("enums_storage");
        for (auto const &param : *desc->params) {
            GParamSpec *spec = param_desc_to_spec(param, self->enums_storage);
            g_object_class_install_property(gobject_class, ++property_id, spec);
        }
    }

    // Install additional parameters AFTER parameters from transform description
    if (desc->flags & ELEMENT_FLAG_SHARABLE) {
        property_id++;
        g_object_class_install_property(
            gobject_class, property_id,
            g_param_spec_string(param::shared_instance_id, param::shared_instance_id,
                                "Identifier for sharing backend instance between multiple elements, for example in "
                                "elements processing multiple inputs",
                                "", G_PARAM_READWRITE));
    }
}

static const GTypeInfo gst_dls_transform_type_info = {.class_size = sizeof(GstDlsTransformClass),
                                                      .base_init = NULL,
                                                      .base_finalize = NULL,
                                                      .class_init = GstDlsTransformClass::init,
                                                      .class_finalize = NULL, // GstDlsTransformClass::finalize TODO
                                                      .class_data = NULL,
                                                      .instance_size = sizeof(GstDlsTransform::GstPodData),
                                                      .n_preallocs = 0,
                                                      .instance_init = GstDlsTransform::instance_init,
                                                      .value_table = NULL};

} // namespace dlstreamer

///////////////////////////////////////////////////////////////////////////////////////

gboolean register_element_gst_plugin(const dlstreamer::ElementDesc *element, GstPlugin *plugin) {
    // make sure Deep Learning Streamer (DL Streamer) metadata registered
    gst_gva_tensor_meta_get_info();
    gst_gva_tensor_meta_api_get_type();

    // register DL Streamer element as GStreamer element
    GTypeInfo type_info = dlstreamer::gst_dls_transform_type_info;
    type_info.class_data = element;
    GType gtype = g_type_register_static(GST_TYPE_BASE_TRANSFORM, element->name.data(), &type_info, (GTypeFlags)0);
    gboolean sts = gst_element_register(plugin, element->name.data(), GST_RANK_NONE, gtype);
    if (!sts)
        GST_ERROR("Error registering element %s", element->name.data());
    return sts;
}

gboolean register_elements_gst_plugin(const dlstreamer::ElementDesc **elements, GstPlugin *plugin) {
    for (; *elements; elements++) {
        if (!register_element_gst_plugin(*elements, plugin))
            return FALSE;
    }
    return TRUE;
}
