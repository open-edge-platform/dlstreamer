/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

/**
 * @file gva_json_meta.h
 * @brief This file contains helper functions to control _GstGVAJSONMeta instances
 */

#ifndef __GVA_JSON_META_H__
#define __GVA_JSON_META_H__

#include "gva_export.h"
#include <gst/gst.h>

#define GVA_JSON_META_API_NAME "GstGVAJSONMetaAPI"
#define GVA_JSON_META_IMPL_NAME "GstGVAJSONMeta"

G_BEGIN_DECLS

typedef struct _GstGVAJSONMeta GstGVAJSONMeta;

/**
 * @brief This struct represents JSON metadata and contains instance of parent GstMeta and message
 */
struct _GstGVAJSONMeta {
    GstMeta meta;   /**< parent GstMeta */
    gchar *message; /**< C-string message */
};

/**
 * @brief This function registers, if needed, and returns GstMetaInfo for _GstGVAJSONMeta
 * @return const GstMetaInfo* for registered type
 */
DLS_EXPORT const GstMetaInfo *gst_gva_json_meta_get_info(void);

/**
 * @brief This function registers, if needed, and returns a GType for api "GstGVAJSONMetaAPI" and associate it with
 * GVA_JSON_META_TAG tag
 * @return GType type
 */
DLS_EXPORT GType gst_gva_json_meta_api_get_type(void);

/**
 * @def GST_GVA_JSON_META_INFO
 * @brief This macro calls gst_gva_json_meta_get_info
 * @return const GstMetaInfo* for registered type
 */
#define GST_GVA_JSON_META_INFO (gst_gva_json_meta_get_info())

/**
 * @def GST_GVA_JSON_META_GET
 * @brief This macro retrieves ptr to _GstGVAJSONMeta instance for passed buf
 * @param buf GstBuffer* of which metadata is retrieved
 * @return _GstGVAJSONMeta* instance attached to buf
 */
#define GST_GVA_JSON_META_GET(buf) ((GstGVAJSONMeta *)gst_buffer_get_meta(buf, gst_gva_json_meta_api_get_type()))

/**
 * @def GST_GVA_JSON_META_ITERATE
 * @brief This macro iterates through _GstGVAJSONMeta instances for passed buf, retrieving the next _GstGVAJSONMeta. If
 * state points to NULL, the first _GstGVAJSONMeta is returned
 * @param buf GstBuffer* of which metadata is iterated and retrieved
 * @param state gpointer* that updates with opaque pointer after macro call.
 * @return _GstGVAJSONMeta* instance attached to buf
 */
#define GST_GVA_JSON_META_ITERATE(buf, state)                                                                          \
    ((GstGVAJSONMeta *)gst_buffer_iterate_meta_filtered(buf, state, gst_gva_json_meta_api_get_type()))

/**
 * @def GST_GVA_JSON_META_ADD
 * @brief This macro attaches new _GstGVAJSONMeta instance to passed buf
 * @param buf GstBuffer* to which metadata will be attached
 * @return _GstGVAJSONMeta* of the newly added instance attached to buf
 */
#define GST_GVA_JSON_META_ADD(buf) ((GstGVAJSONMeta *)gst_buffer_add_meta(buf, gst_gva_json_meta_get_info(), NULL))

/**
 * @brief This function returns message field of _GstGVAJSONMeta
 * @param meta _GstGVAJSONMeta* to retrieve message of
 * @return C-style string with message
 */
gchar *get_json_message(GstGVAJSONMeta *meta);

/**
 * @brief This function sets message field of _GstGVAJSONMeta
 * @param meta _GstGVAJSONMeta* to set message
 * @param message message
 * @return void
 */
void set_json_message(GstGVAJSONMeta *meta, const gchar *message);

G_END_DECLS

#endif /* __GVA_JSON_META_H__ */
