/*******************************************************************************
 * Copyright (C) 2022-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "dlstreamer/base/context.h"
#include "dlstreamer/cpu/context.h"
#include "dlstreamer/dma/context.h"
#include "dlstreamer/gst/mappers/gst_to_cpu.h"
#include "dlstreamer/gst/mappers/gst_to_opencl.h"
#include "dlstreamer/gst/utils.h"
#include "dlstreamer/utils.h"
#ifndef _WIN32
#include "dlstreamer/gst/mappers/gst_to_vaapi.h"
#else
#include "dlstreamer/gst/mappers/gst_to_d3d11.h"
#endif

namespace dlstreamer {

class GSTContextQuery : public BaseContext {
  public:
    GSTContextQuery(GstPad *pad, MemoryType memory_type, const char *context_name = nullptr)
        : BaseContext(memory_type) {
        if (!context_name)
            context_name = get_context_name(memory_type);
        query_context(pad, context_name);
    }
    GSTContextQuery(GstElement *element, MemoryType memory_type, const char *context_name = nullptr)
        : BaseContext(memory_type) {
        if (!context_name)
            context_name = get_context_name(memory_type);
        DLS_CHECK(_context = gst_element_get_context(element, context_name));
    }
    GSTContextQuery(GstBaseTransform *element, MemoryType memory_type, const char *context_name = nullptr)
        : BaseContext(memory_type) {
        if (!context_name)
            context_name = get_context_name(memory_type);
        query_context(element->sinkpad, context_name);
    }

    ~GSTContextQuery() {
        if (_context)
            gst_context_unref(_context);
    }

    handle_t handle(std::string_view key) const noexcept override {
        handle_t value = 0;
#ifndef _WIN32
        if (key == BaseContext::key::va_display) {
            GstObject *display_obj = nullptr;
            if (gst_structure_get(_structure, VAAPI_DISPLAY_FIELD_NAME, GST_TYPE_OBJECT, &display_obj, NULL)) {
                g_object_get(display_obj, VAAPI_DISPLAY_PROPERTY_NAME, &value, nullptr);
                gst_object_unref(display_obj);
                GST_INFO("Got VADisplay from VAAPI context: %p", value);
            }
            if (gst_structure_get(_structure, VA_DISPLAY_FIELD_NAME, GST_TYPE_OBJECT, &display_obj, NULL)) {
                g_object_get(display_obj, VA_DISPLAY_PROPERTY_NAME, &value, nullptr);
                gst_object_unref(display_obj);
                GST_INFO("Got VADisplay from VA context: %p", value);
            }
            return value;
        }
#else
        if (key == BaseContext::key::d3d_device) {
            GstD3D11Device *d3d11_device = NULL;
            if (gst_structure_get(_structure, "device", GST_TYPE_D3D11_DEVICE, &d3d11_device, NULL)) {
                value = reinterpret_cast<handle_t>(d3d11_device);
                // gst_clear_object(&d3d11_device);
            }
            return value;
        }
#endif
        if (!gst_structure_get(_structure, key.data(), G_TYPE_POINTER, &value, NULL)) {
            GST_ERROR("Invalid gst_structure_get() method field(s) requested");
        }
        return value;
    }

  private:
    GstContext *_context = nullptr;
    const GstStructure *_structure = nullptr;

    static constexpr auto VAAPI_CONTEXT_NAME = "gst.vaapi.Display";
    static constexpr auto VAAPI_DISPLAY_FIELD_NAME = "gst.vaapi.Display.GObject";
    static constexpr auto VAAPI_DISPLAY_PROPERTY_NAME = "va-display";

    static constexpr auto VA_CONTEXT_NAME = "gst.va.display.handle";
    static constexpr auto VA_DISPLAY_FIELD_NAME = "gst-display";
    static constexpr auto VA_DISPLAY_PROPERTY_NAME = "va-display";

    static constexpr auto D3D11_CONTEXT_NAME = "gst.d3d11.device.handle";

    const char *get_context_name(MemoryType memory_type) {
#ifndef _WIN32
        if (memory_type == MemoryType::VA) {
            // Load GST-VA and reuse VAAPI path.
            set_memory_type(MemoryType::VAAPI);
            return VA_CONTEXT_NAME;
        }
        if (memory_type == MemoryType::VAAPI) {
            GST_ELEMENT_WARNING(_context, LIBRARY, INIT, ("VASurface and Gst-VAAPI is deprecated."),
                                ("%s", "Please use VAMemory na Gst-VA instead."));
            return VAAPI_CONTEXT_NAME;
        }
#else
        if (memory_type == MemoryType::D3D11) {
            set_memory_type(MemoryType::D3D11);
            return D3D11_CONTEXT_NAME;
        }
#endif
        return memory_type_to_string(memory_type);
    }

    void query_context(GstPad *pad, const gchar *context_name) {
        GstQuery *query = gst_query_new_context(context_name);
        auto query_unref = std::shared_ptr<GstQuery>(query, [](GstQuery *query) { gst_query_unref(query); });

        gboolean ret = gst_pad_peer_query(pad, query);
        if (!ret)
            throw std::runtime_error("Couldn't query GST context: " + std::string(context_name));

        gst_query_parse_context(query, &_context);
        if (!_context)
            throw std::runtime_error("Error gst_query_parse_context");
        else
            GST_INFO_OBJECT(pad, "Got GST context: %" GST_PTR_FORMAT, _context);

        gst_context_ref(_context);
        _structure = gst_context_get_structure(_context);
    }
};

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class GSTContext : public BaseContext {
  public:
    GSTContext(GstElement *element) : BaseContext(MemoryType::GST), _element(element) {
    }

    ContextPtr derive_context(MemoryType memory_type) noexcept override {
        if (memory_type == MemoryType::CPU)
            return std::make_shared<CPUContext>();
        if (memory_type == MemoryType::DMA)
            return std::make_shared<DMAContext>();
        // try query context from all pads in element (source and sink)
        for (GList *item = _element->pads; item; item = item->next) {
            try {
                auto pad = static_cast<GstPad *>(item->data);
                return std::make_shared<GSTContextQuery>(pad, memory_type);
            } catch (...) {
            }
        }
        return nullptr;
    }

    MemoryMapperPtr get_mapper(const ContextPtr &input_context, const ContextPtr &output_context) override {
        MemoryMapperPtr mapper = BaseContext::get_mapper(input_context, output_context);
        if (mapper)
            return mapper;

        auto input_type = input_context ? input_context->memory_type() : MemoryType::CPU;
        auto output_type = output_context ? output_context->memory_type() : MemoryType::CPU;
        if (input_type == MemoryType::GST && output_type == MemoryType::CPU)
            mapper = std::make_shared<MemoryMapperGSTToCPU>(input_context, output_context);
#ifndef _WIN32
        if (input_type == MemoryType::GST && output_type == MemoryType::VAAPI)
            mapper = std::make_shared<MemoryMapperGSTToVAAPI>(input_context, output_context);
#else
        if (input_type == MemoryType::GST && output_type == MemoryType::D3D11)
            mapper = std::make_shared<MemoryMapperGSTToD3D11>(input_context, output_context);
#endif
        if (input_type == MemoryType::GST && output_type == MemoryType::OpenCL)
            mapper = std::make_shared<MemoryMapperGSTToOpenCL>(input_context, output_context);

        BaseContext::attach_mapper(mapper);
        return mapper;
    }

  private:
    GstElement *_element = nullptr;
};

} // namespace dlstreamer
