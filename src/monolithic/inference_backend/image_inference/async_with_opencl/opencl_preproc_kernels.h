/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

// OpenCL C sources of the OpenCL pre-processing backend.
//
// One work item per tensor pixel does the whole pre-processing of that pixel:
// colour conversion, crop, resize and padding, and writes it to the three
// planes of an u8 NCHW tensor (C = 3) in the model's channel order. Pixels
// outside `dst_rect` are padding.
//
// The source planes are shared D3D11 texture planes imported through
// cl_khr_d3d11_sharing (+ cl_intel_d3d11_nv12_media_sharing for NV12):
//
//   NV12 Y  plane -> CL_R    / CL_UNORM_INT8
//   NV12 UV plane -> CL_RG   / CL_UNORM_INT8, half resolution
//   RGBA / BGRA   -> CL_RGBA / CL_BGRA, CL_UNORM_INT8 (read_imagef returns RGBA either way)
//
// and are resampled bilinearly by the sampler. `src_map` maps a tensor pixel
// to the source: (x0, y0) is the source position of the left/top edge of
// `dst_rect` and (sx, sy) the source pixels per tensor pixel. The clamp windows
// keep the filter inside the frame, so it does not blend in the padding rows
// and columns of decoder surfaces.

namespace InferenceBackend {
namespace opencl_preproc {

// clang-format off
static const char *const KERNELS = R"CLC(

__constant sampler_t PP_LINEAR = CLK_NORMALIZED_COORDS_FALSE | CLK_ADDRESS_CLAMP_TO_EDGE | CLK_FILTER_LINEAR;

inline bool pp_is_padding(int x, int y, int4 r) {
    return x < r.x || y < r.y || x >= r.x + r.z || y >= r.y + r.w;
}

inline float2 pp_src_pos(int x, int y, int4 r, float4 src_map) {
    return (float2)(src_map.x + ((float)(x - r.x) + 0.5f) * src_map.z,
                    src_map.y + ((float)(y - r.y) + 0.5f) * src_map.w);
}

inline void pp_store(__global uchar *dst, int dst_w, int dst_h, int x, int y, float3 v) {
    const size_t plane = (size_t)dst_w * dst_h;
    const size_t idx = (size_t)y * dst_w + x;
    dst[idx] = convert_uchar_sat_rte(v.x);
    dst[plane + idx] = convert_uchar_sat_rte(v.y);
    dst[2 * plane + idx] = convert_uchar_sat_rte(v.z);
}

// Output channel c = dot(m<c>, (Y, U, V, 1)) * 255, with Y, U and V normalized
// to [0, 1]. The rows are in the model's channel order, so this also does the
// RGB/BGR swap. `fill` is the padding colour in that order, in [0, 255].
__kernel void pp_nv12(__read_only image2d_t y_plane, __read_only image2d_t uv_plane,
                      __global uchar *dst, int dst_w, int dst_h, int4 dst_rect, float4 src_map,
                      float4 y_clamp, float4 uv_clamp, float4 m0, float4 m1, float4 m2, float4 fill) {
    const int x = get_global_id(0);
    const int y = get_global_id(1);
    if (x >= dst_w || y >= dst_h)
        return;

    float3 v;
    if (pp_is_padding(x, y, dst_rect)) {
        v = fill.xyz;
    } else {
        const float2 p = pp_src_pos(x, y, dst_rect, src_map);
        const float luma = read_imagef(y_plane, PP_LINEAR, clamp(p, y_clamp.xy, y_clamp.zw)).x;
        const float2 chroma = read_imagef(uv_plane, PP_LINEAR, clamp(p * 0.5f, uv_clamp.xy, uv_clamp.zw)).xy;
        const float4 yuv1 = (float4)(luma, chroma, 1.f);
        v = (float3)(dot(m0, yuv1), dot(m1, yuv1), dot(m2, yuv1)) * 255.f;
    }
    pp_store(dst, dst_w, dst_h, x, y, v);
}

// `bgr` selects the output channel order.
__kernel void pp_rgba(__read_only image2d_t src, __global uchar *dst, int dst_w, int dst_h, int4 dst_rect,
                      float4 src_map, float4 src_clamp, int bgr, float4 fill) {
    const int x = get_global_id(0);
    const int y = get_global_id(1);
    if (x >= dst_w || y >= dst_h)
        return;

    float3 v;
    if (pp_is_padding(x, y, dst_rect)) {
        v = fill.xyz;
    } else {
        const float2 p = pp_src_pos(x, y, dst_rect, src_map);
        const float4 c = read_imagef(src, PP_LINEAR, clamp(p, src_clamp.xy, src_clamp.zw)) * 255.f;
        v = bgr ? c.zyx : c.xyz;
    }
    pp_store(dst, dst_w, dst_h, x, y, v);
}

)CLC";
// clang-format on

} // namespace opencl_preproc
} // namespace InferenceBackend
