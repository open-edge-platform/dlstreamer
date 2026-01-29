/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

/**
 * @file gva_tensor_meta.h
 * @brief Metadata containing GstStructure
 */

#ifndef __GVA_TENSOR_META_H__
#define __GVA_TENSOR_META_H__

#include "gva_export.h"
#include <gst/gst.h>

#define GVA_TENSOR_META_API_NAME "GstGVATensorMetaAPI"
#define GVA_TENSOR_META_IMPL_NAME "GstGVATensorMeta"

#define GVA_TENSOR_MAX_RANK 8

G_BEGIN_DECLS

/**
 * @brief This enum describes model layer precision
 */
typedef enum {
    GVA_PRECISION_UNSPECIFIED = 255, /**< default value */
    GVA_PRECISION_FP32 = 10,         /**< 32bit floating point value */
    GVA_PRECISION_FP16 = 11,         /**< 16bit floating point value, 5 bit for exponent, 10 bit for mantisa */
    GVA_PRECISION_BF16 = 12,         /**< 16bit floating point value, 8 bit for exponent, 7 bit for mantisa*/
    GVA_PRECISION_FP64 = 13,         /**< 64bit floating point value */
    GVA_PRECISION_Q78 = 20,          /**< 16bit specific signed fixed point precision */
    GVA_PRECISION_I16 = 30,          /**< 16bit signed integer value */
    GVA_PRECISION_U4 = 39,           /**< 4bit unsigned integer value */
    GVA_PRECISION_U8 = 40,           /**< unsignned 8bit integer value */
    GVA_PRECISION_I4 = 49,           /**< 4bit signed integer value */
    GVA_PRECISION_I8 = 50,           /**< 8bit signed integer value */
    GVA_PRECISION_U16 = 60,          /**< 16bit unsigned integer value */
    GVA_PRECISION_I32 = 70,          /**< 32bit signed integer value */
    GVA_PRECISION_U32 = 74,          /**< 32bit unsigned integer value */
    GVA_PRECISION_I64 = 72,          /**< 64bit signed integer value */
    GVA_PRECISION_U64 = 73,          /**< 64bit unsigned integer value */
    GVA_PRECISION_BIN = 71,          /**< 1bit integer value */
    GVA_PRECISION_BOOL = 41,         /**< 8bit bool type */
    GVA_PRECISION_CUSTOM = 80        /**< custom precision has it's own name and size of elements */
} GVAPrecision;

/**
 * @brief This enum describes model layer layout
 */
typedef enum {
    GVA_LAYOUT_ANY = 0,  /**< unspecified layout */
    GVA_LAYOUT_NCHW = 1, /**< NCWH layout */
    GVA_LAYOUT_NHWC = 2, /**< NHWC layout */
    GVA_LAYOUT_NC = 193  /**< NC layout */
} GVALayout;

/**
 * @brief This function returns a pointer to the fixed array of tensor bytes
 * @param s GstStructure* to get tensor from. It's assumed that tensor data is stored in "data_buffer" field
 * @param[out] nbytes pointer to the location to store the number of bytes in returned array
 * @return void* to tensor data as bytes, NULL if s has no "data_buffer" field
 */
inline const void *gva_get_tensor_data(GstStructure *s, gsize *nbytes) {
    const GValue *f = gst_structure_get_value(s, "data_buffer");
    if (!f)
        return NULL;
    GVariant *v = g_value_get_variant(f);
    return g_variant_get_fixed_array(v, nbytes, 1);
}

typedef struct _GstGVATensorMeta GstGVATensorMeta;

/**
 * @brief This struct represents raw tensor metadata and contains instance of parent GstMeta and fields describing
 * inference result tensor. This metadata instances is attached to buffer by gvainference elements
 */
struct _GstGVATensorMeta {
    GstMeta meta;       /**< parent meta object */
    GstStructure *data; /**< pointer to GstStructure data contains: precision, rank, array tensor's dimensions, layout,
                    output layer name, model name, tensor data, tensor size, id of GStreamer pipeline element */
};

/**
 * @brief This function registers, if needed, and returns GstMetaInfo for _GstGVATensorMeta
 * @return GstMetaInfo* for registered type
 */
DLS_EXPORT const GstMetaInfo *gst_gva_tensor_meta_get_info(void);

/**
 * @brief This function registers, if needed, and returns a GType for api "GstGVATensorMetaAPI" and associate it with
 * GVA_TENSOR_META_TAG tag
 * @return GType type
 */
DLS_EXPORT GType gst_gva_tensor_meta_api_get_type(void);

/**
 * @def GST_GVA_TENSOR_META_INFO
 * @brief This macro calls gst_gva_tensor_meta_get_info
 * @return const GstMetaInfo* for registered type
 */
#define GST_GVA_TENSOR_META_INFO (gst_gva_tensor_meta_get_info())

/**
 * @def GST_GVA_TENSOR_META_GET
 * @brief This macro retrieves ptr to _GstGVATensorMeta instance for passed buf
 * @param buf GstBuffer* of which metadata is retrieved
 * @return _GstGVATensorMeta* instance attached to buf
 */
#define GST_GVA_TENSOR_META_GET(buf) ((GstGVATensorMeta *)gst_buffer_get_meta(buf, gst_gva_tensor_meta_api_get_type()))

/**
 * @def GST_GVA_TENSOR_META_ITERATE
 * @brief This macro iterates through _GstGVATensorMeta instances for passed buf, retrieving the next _GstGVATensorMeta.
 * If state points to NULL, the first _GstGVATensorMeta is returned
 * @param buf GstBuffer* of which metadata is iterated and retrieved
 * @param state gpointer* that updates with opaque pointer after macro call.
 * @return _GstGVATensorMeta* instance attached to buf
 */
#define GST_GVA_TENSOR_META_ITERATE(buf, state)                                                                        \
    ((GstGVATensorMeta *)gst_buffer_iterate_meta_filtered(buf, state, gst_gva_tensor_meta_api_get_type()))

/**
 * @def GST_GVA_TENSOR_META_ADD
 * @brief This macro attaches new _GstGVATensorMeta instance to passed buf
 * @param buf GstBuffer* to which metadata will be attached
 * @return _GstGVATensorMeta* of the newly added instance attached to buf
 */
#define GST_GVA_TENSOR_META_ADD(buf)                                                                                   \
    ((GstGVATensorMeta *)gst_buffer_add_meta(buf, gst_gva_tensor_meta_get_info(), NULL))

/**
 * @def GST_GVA_TENSOR_META_COUNT
 * @brief This macro counts the number of _GstGVATensorMeta instances attached to passed buf
 * @param buf GstBuffer* of which metadata instances are counted
 * @return guint number of _GstGVATensorMeta instances attached to passed buf
 */
#define GST_GVA_TENSOR_META_COUNT(buf) (gst_buffer_get_n_meta(buf, gst_gva_tensor_meta_api_get_type()))

/**
 * @brief This function searches for first _GstGVATensorMeta instance that satisfies passed parameters
 * @param buffer GstBuffer* that is searched for metadata
 * @param model_name substring that should be in _GstGVATensorMeta instance's model_name
 * @param output_layer substring that should be in _GstGVATensorMeta instance's layer_name
 * @return GstGVATensorMeta* for found instance or NULL if none are found
 */
GstGVATensorMeta *find_tensor_meta(GstBuffer *buffer, const char *model_name, const char *output_layer);

/**
 * @brief This function searches for first _GstGVATensorMeta instance that satisfies passed parameters
 * @param buffer GstBuffer* that is searched for metadata
 * @param model_name substring that should be in _GstGVATensorMeta instance's model_name
 * @param output_layer substring that should be in _GstGVATensorMeta instance's layer_name
 * @param element_id element_id substring that should be in _GstGVATensorMeta instance's element_id
 * @return GstGVATensorMeta* for found instance or NULL if none are found
 */
GstGVATensorMeta *find_tensor_meta_ext(GstBuffer *buffer, const char *model_name, const char *output_layer,
                                       const char *element_id);

G_END_DECLS

#endif /* __GVA_TENSOR_META_H__ */
