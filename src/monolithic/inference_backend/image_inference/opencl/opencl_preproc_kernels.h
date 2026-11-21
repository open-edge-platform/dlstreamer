/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include <cstdint>

// OpenCL C sources of the OpenCL pre-processing backend (pre-process-backend=opencl).
//
// A launch pre-processes a group of ROIs of one frame. Work item (x, y, i) computes pixel (x, y) of the model input
// of ROI rois[roi_offset + i], i.e. does the color conversion, crop, resize and padding of that pixel, and writes it
// to the three planes of batch element rois[...].index of the u8 NCHW model input, in the model's channel order.
//
// The source is sampled at pixel centers and filtered bilinearly. `clamp0` and `clamp1` keep the filter inside the
// frame (plane 0 and the chroma planes), so it does not blend in the padding rows and columns of decoder surfaces.
// With s the source pixel, normalized to [0, 1] and extended with 1, output channel c is dot(m<c>, s), in [0, 255]:
// for YUV sources s = (Y, U, V, 1) and the rows convert the color; for RGB sources s holds the channels in the order
// of the source and the rows pick them in the model's order.
//
// Sources:
//   pp_nv12   Y plane (CL_R) and interleaved UV plane (CL_RG, half resolution) images
//   pp_i420   Y, U and V plane images (CL_R, chroma at half resolution)
//   pp_rgbx   one image with 4 channels per pixel
//   pp_rgb24  a buffer with 3 bytes per pixel, rows `src_stride` bytes apart (no image format holds it)

namespace InferenceBackend {
namespace opencl_preproc {

// Must match pp_roi in KERNELS: four 16-byte vectors, then the batch index padded to 16 bytes.
struct RoiParams {
    int32_t dst_rect[4];  // x, y, width, height of the resized image in the model input; the rest is padding
    float src_map[4];     // x0, y0, sx, sy: source position of the left/top edge of dst_rect, source pixels per pixel
    float fill[4];        // padding color, in model channel order, in [0, 255]
    uint32_t index;       // batch element the ROI is written to
    uint32_t reserved[3]; // padding to the size of the OpenCL struct
};
static_assert(sizeof(RoiParams) == 64, "RoiParams must match pp_roi of the OpenCL kernels");

// clang-format off
static const char *const KERNELS = R"CLC(

typedef struct {
    int4 dst_rect;
    float4 src_map;
    float4 fill;
    uint index;
    uint reserved0, reserved1, reserved2;
} pp_roi;

__constant sampler_t PP_LINEAR = CLK_NORMALIZED_COORDS_FALSE | CLK_ADDRESS_CLAMP_TO_EDGE | CLK_FILTER_LINEAR;

inline bool pp_is_padding(int x, int y, int4 r) {
    return x < r.x || y < r.y || x >= r.x + r.z || y >= r.y + r.w;
}

// Source position, in pixels of plane 0, of the center of model input pixel (x, y).
inline float2 pp_src_pos(int x, int y, int4 dst_rect, float4 src_map) {
    return (float2)(src_map.x + ((float)(x - dst_rect.x) + 0.5f) * src_map.z,
                    src_map.y + ((float)(y - dst_rect.y) + 0.5f) * src_map.w);
}

inline void pp_store(__global uchar *dst, int dst_w, int dst_h, uint index, int x, int y, float3 v) {
    const size_t plane = (size_t)dst_w * dst_h;
    const size_t at = (size_t)index * 3 * plane + (size_t)y * dst_w + x;
    dst[at] = convert_uchar_sat_rte(v.x);
    dst[at + plane] = convert_uchar_sat_rte(v.y);
    dst[at + 2 * plane] = convert_uchar_sat_rte(v.z);
}

inline float3 pp_apply(float4 s, float4 m0, float4 m1, float4 m2) {
    return (float3)(dot(m0, s), dot(m1, s), dot(m2, s));
}

#define PP_BEGIN                                                    \
    const int x = get_global_id(0);                                 \
    const int y = get_global_id(1);                                 \
    if (x >= dst_w || y >= dst_h)                                   \
        return;                                                     \
    const pp_roi r = rois[roi_offset + get_global_id(2)];           \
    if (pp_is_padding(x, y, r.dst_rect)) {                          \
        pp_store(dst, dst_w, dst_h, r.index, x, y, r.fill.xyz);     \
        return;                                                     \
    }                                                               \
    const float2 p = pp_src_pos(x, y, r.dst_rect, r.src_map);

#define PP_ARGS                                                                                         \
    __global const pp_roi *rois, uint roi_offset, __global uchar *dst, int dst_w, int dst_h,           \
    float4 clamp0, float4 clamp1, float4 m0, float4 m1, float4 m2

__kernel void pp_nv12(__read_only image2d_t y_plane, __read_only image2d_t uv_plane, PP_ARGS) {
    PP_BEGIN
    const float luma = read_imagef(y_plane, PP_LINEAR, clamp(p, clamp0.xy, clamp0.zw)).x;
    const float2 chroma = read_imagef(uv_plane, PP_LINEAR, clamp(p * 0.5f, clamp1.xy, clamp1.zw)).xy;
    pp_store(dst, dst_w, dst_h, r.index, x, y, pp_apply((float4)(luma, chroma, 1.f), m0, m1, m2));
}

__kernel void pp_i420(__read_only image2d_t y_plane, __read_only image2d_t u_plane, __read_only image2d_t v_plane,
                      PP_ARGS) {
    PP_BEGIN
    const float2 pc = clamp(p * 0.5f, clamp1.xy, clamp1.zw);
    const float luma = read_imagef(y_plane, PP_LINEAR, clamp(p, clamp0.xy, clamp0.zw)).x;
    const float u = read_imagef(u_plane, PP_LINEAR, pc).x;
    const float v = read_imagef(v_plane, PP_LINEAR, pc).x;
    pp_store(dst, dst_w, dst_h, r.index, x, y, pp_apply((float4)(luma, u, v, 1.f), m0, m1, m2));
}

__kernel void pp_rgbx(__read_only image2d_t src, PP_ARGS) {
    PP_BEGIN
    const float4 c = read_imagef(src, PP_LINEAR, clamp(p, clamp0.xy, clamp0.zw));
    pp_store(dst, dst_w, dst_h, r.index, x, y, pp_apply((float4)(c.xyz, 1.f), m0, m1, m2));
}

inline float3 pp_load3(__global const uchar *src, int src_stride, int x, int y) {
    return convert_float3(vload3(0, src + (size_t)y * src_stride + (size_t)x * 3));
}

// Bilinear filtering as the image sampler does it, on a buffer.
__kernel void pp_rgb24(__global const uchar *src, int src_stride, int src_w, int src_h, PP_ARGS) {
    PP_BEGIN
    const float2 q = clamp(p, clamp0.xy, clamp0.zw) - 0.5f;
    const float2 f = floor(q);
    const float2 a = q - f;
    const int x0 = clamp((int)f.x, 0, src_w - 1);
    const int y0 = clamp((int)f.y, 0, src_h - 1);
    const int x1 = min(x0 + 1, src_w - 1);
    const int y1 = min(y0 + 1, src_h - 1);
    const float3 top = mix(pp_load3(src, src_stride, x0, y0), pp_load3(src, src_stride, x1, y0), a.x);
    const float3 bottom = mix(pp_load3(src, src_stride, x0, y1), pp_load3(src, src_stride, x1, y1), a.x);
    const float3 c = mix(top, bottom, a.y) * (1.f / 255.f);
    pp_store(dst, dst_w, dst_h, r.index, x, y, pp_apply((float4)(c, 1.f), m0, m1, m2));
}

)CLC";
// clang-format on

} // namespace opencl_preproc
} // namespace InferenceBackend
