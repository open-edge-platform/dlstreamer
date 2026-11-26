/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "gvametapublishfile.hpp"

#include <common.hpp>

#include <filesystem>
#include <stdio.h>
#include <string>

#ifdef _MSC_VER
// MSVC alternative for gcc ftello
#define ftello _ftelli64
#endif

GST_DEBUG_CATEGORY_STATIC(gva_meta_publish_file_debug_category);
#define GST_CAT_DEFAULT gva_meta_publish_file_debug_category

namespace {

constexpr auto JSON_RECORD_PREFIX = ",\n";
constexpr auto JSON_LINES_RECORD_SUFFIX = "\n";

} // namespace

/* Properties */
enum {
    PROP_0,
    PROP_FILE_PATH,
    PROP_FILE_FORMAT,
};

class GvaMetaPublishFilePrivate {
  private:
    void write_message_prefix() {
        // Add comma and line feed before the record when producing a JSON
        if (_file_format == GVA_META_PUBLISH_JSON) {
            if (ftello(_output_file) > 2) {
                // a prior record was written, precede this message with record separator
                fputs(JSON_RECORD_PREFIX, _output_file);
            }
        }
    }

    void write_message_suffix() {
        // Add line feed after each record when producing a JSON Lines file/FIFO
        if (_file_format == GVA_META_PUBLISH_JSON_LINES) {
            fputs(JSON_LINES_RECORD_SUFFIX, _output_file);
        }
    }

    bool write_message(const std::string &message) {
        if (!_output_file)
            return false;
        write_message_prefix();
        fputs(message.c_str(), _output_file);
        write_message_suffix();
        fflush(_output_file);
        return true;
    }

    bool finalize_file() {
        if (!_output_file) {
            return false;
        }
        if (_file_format == GVA_META_PUBLISH_JSON && ftello(_output_file) > 0) {
            fputs("]", _output_file);
        }
        fputs("\n", _output_file);
        // For any pathfile we initialized w/ fopen(), invoke corresponding fclose()
        if (_file_path != STDOUT) {
            if (fclose(_output_file) != 0) {
                return false;
            }
        }
        return true;
    }

    bool initialize_file() {
        if (_file_path == STDOUT) {
            _output_file = stdout;
            return true;
        }
        if (std::filesystem::is_symlink(_file_path)) {
            // refuse to read/write through a symlink planted at the output path
            return false;
        }
        if (_file_format == GVA_META_PUBLISH_JSON) {
            if (!(_output_file = fopen(_file_path.c_str(), "w+"))) {
                return false;
            }
            // File will be an array of JSON objects. Start the array with '['
            fputs("[", _output_file);
        } else { // GVA_META_PUBLISH_JSON_LINES
            if (!(_output_file = fopen(_file_path.c_str(), "a+"))) {
                return false;
            }
        }
        return true;
    }

  public:
    GvaMetaPublishFilePrivate(GvaMetaPublishBase *base) : _base(base) {
    }

    ~GvaMetaPublishFilePrivate() = default;

    gboolean start() {
        if (_file_path.empty()) {
            GST_ELEMENT_ERROR(_base, RESOURCE, NOT_FOUND, ("file_path cannot be NULL."), (NULL));
            return false;
        }
        if (!initialize_file()) {
            GST_ELEMENT_ERROR(_base, RESOURCE, NOT_FOUND, ("Error opening file %s.", _file_path.c_str()), (nullptr));
            return false;
        }
        return true;
    }

    gboolean stop() {
        if (!finalize_file()) {
            GST_ERROR_OBJECT(_base, "Error finalizing file.");
            return false;
        }
        GST_DEBUG_OBJECT(_base, "File finalized successfully.");
        return true;
    }

    gboolean publish(const std::string &message) {
        if (!write_message(message)) {
            GST_ERROR_OBJECT(_base, "Error writing inference to file.");
            return false;
        }

        GST_DEBUG_OBJECT(_base, "Message was written successfully.");

        return true;
    }

    bool get_property(guint prop_id, GValue *value) {
        switch (prop_id) {
        case PROP_FILE_PATH:
            g_value_set_string(value, _file_path.c_str());
            break;
        case PROP_FILE_FORMAT:
            g_value_set_enum(value, _file_format);
            break;
        default:
            return false;
        }
        return true;
    }

    bool set_property(guint prop_id, const GValue *value) {
        switch (prop_id) {
        case PROP_FILE_PATH:
            _file_path = g_value_get_string(value);
            break;
        case PROP_FILE_FORMAT:
            _file_format = static_cast<FileFormat>(g_value_get_enum(value));
            break;
        default:
            return false;
        }
        return true;
    }

  private:
    GvaMetaPublishBase *_base;

    std::string _file_path;
    FileFormat _file_format = GVA_META_PUBLISH_JSON;
    FILE *_output_file = nullptr;
};

G_DEFINE_TYPE_EXTENDED(GvaMetaPublishFile, gva_meta_publish_file, GST_TYPE_GVA_META_PUBLISH_BASE, 0,
                       G_ADD_PRIVATE(GvaMetaPublishFile);
                       GST_DEBUG_CATEGORY_INIT(gva_meta_publish_file_debug_category, "gvametapublishfile", 0,
                                               "debug category for gvametapublishfile element"));

static void gva_meta_publish_file_init(GvaMetaPublishFile *self) {
    // Initialize of private data
    auto *priv_memory = gva_meta_publish_file_get_instance_private(self);
    // This won't be converted to shared ptr because of memory placement
    self->impl = new (priv_memory) GvaMetaPublishFilePrivate(&self->base);
}

static void gva_meta_publish_file_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) {
    auto self = GVA_META_PUBLISH_FILE(object);

    if (!self->impl->get_property(prop_id, value))
        G_OBJECT_CLASS(gva_meta_publish_file_parent_class)->get_property(object, prop_id, value, pspec);
}

static void gva_meta_publish_file_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) {
    auto self = GVA_META_PUBLISH_FILE(object);

    if (!self->impl->set_property(prop_id, value))
        G_OBJECT_CLASS(gva_meta_publish_file_parent_class)->set_property(object, prop_id, value, pspec);
}

static void gva_meta_publish_file_finalize(GObject *object) {
    auto self = GVA_META_PUBLISH_FILE(object);
    g_assert(self->impl && "Expected valid 'impl' pointer during finalize");

    if (self->impl) {
        // Destroy C++ structure manually
        self->impl->~GvaMetaPublishFilePrivate();
        self->impl = nullptr;
    }
}

static void gva_meta_publish_file_class_init(GvaMetaPublishFileClass *klass) {
    auto gobject_class = G_OBJECT_CLASS(klass);
    auto base_transform_class = GST_BASE_TRANSFORM_CLASS(klass);
    auto base_metapublish_class = GVA_META_PUBLISH_BASE_CLASS(klass);

    gobject_class->set_property = gva_meta_publish_file_set_property;
    gobject_class->get_property = gva_meta_publish_file_get_property;
    gobject_class->finalize = gva_meta_publish_file_finalize;

    base_transform_class->start = [](GstBaseTransform *base) { return GVA_META_PUBLISH_FILE(base)->impl->start(); };
    base_transform_class->stop = [](GstBaseTransform *base) { return GVA_META_PUBLISH_FILE(base)->impl->stop(); };

    base_metapublish_class->publish = [](GvaMetaPublishBase *base, const std::string &message) {
        return GVA_META_PUBLISH_FILE(base)->impl->publish(message);
    };

    gst_element_class_set_static_metadata(GST_ELEMENT_CLASS(klass), "File metadata publisher", "Metadata",
                                          "Publishes the JSON metadata to files", "Intel Corporation");

    auto prm_flags = static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT);
    g_object_class_install_property(gobject_class, PROP_FILE_PATH,
                                    g_param_spec_string("file-path", "FilePath",
                                                        "Absolute path to output file for publishing inferences.",
                                                        DEFAULT_FILE_PATH, prm_flags));
    g_object_class_install_property(
        gobject_class, PROP_FILE_FORMAT,
        g_param_spec_enum("file-format", "File Format", "Structure of JSON objects in the file",
                          GST_TYPE_GVA_METAPUBLISH_FILE_FORMAT, DEFAULT_FILE_FORMAT, prm_flags));
}
