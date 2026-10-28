/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

// OpenCL C sources for the plane-native watermark renderer.
//
// Every kernel takes a single plane as a `__write_only image2d_t` plus that
// plane's dimensions, and a `wm_prim` array shared by all kernels. The host
// packs plane-specific geometry and an already-converted, already-normalized
// colour into `wm_prim`, so the kernels never do colour conversion and never
// need to know which plane they are writing:
//
//   NV12 Y  plane -> image is CL_R    / CL_UNORM_INT8, colour = (Y, 0, 0, 0)
//   NV12 UV plane -> image is CL_RG   / CL_UNORM_INT8, colour = (U, V, 0, 0)
//   packed 4-byte -> image is CL_RGBA / CL_UNORM_INT8, colour = the four bytes
//                    the CPU renderer would write, in memory order
//
// The images come from clCreateFromVA_APIMediaSurfaceINTEL and are UNORM, so
// all writes go through write_imagef with values normalized to [0, 1].
// write_imageui compiles but silently writes zeros.

namespace renderer_gpu {

// clang-format off
static const char *const KERNELS = R"CLC(

typedef struct {
    int4   a;      // outline/fill: x0, y0, x1, y1 (inclusive)
                   // span:         x1, x2 (inclusive), y, unused
                   // mask:         dst_x, dst_y, w, h
                   // blur/blend:   x, y, w, h
    int4   b;      // outline: band half-width k
                   // mask:    atlas byte offset, atlas row stride
                   // blur:    kernel width, kernel height, kx offset, ky offset
                   // blend:   blob byte offset, blob row stride
    float4 color;  // plane-packed, normalized to [0, 1]
                   // blur:    x != 0 selects the integer fixed-point regime,
                   //          y is the column at which that regime hands over
                   //          from round-half-even to round-half-up
} wm_prim;

__constant sampler_t WM_SAMPLER = CLK_NORMALIZED_COORDS_FALSE | CLK_ADDRESS_CLAMP_TO_EDGE | CLK_FILTER_NEAREST;

// The channels a read-modify-write kernel has to carry through its scratch
// buffer: two, which covers either NV12 plane. WM_LOAD narrows what
// read_imagef returned, WM_STORE widens it back for write_imagef.
typedef float2 wm_vec;
#define WM_ZERO     ((float2)(0.f, 0.f))
#define WM_LOAD(v)  ((float2)((v).x, (v).y))
#define WM_STORE(s) ((float4)((s).x, (s).y, 0.f, 0.f))

// cv::borderInterpolate(p, len, BORDER_REFLECT_101). Loops, because a
// primitive's Gaussian kernel can be wider than the plane.
inline int wm_reflect101(int p, int len) {
    if (len == 1)
        return 0;
    while (p < 0 || p >= len)
        p = p < 0 ? -p : 2 * len - 2 - p;
    return p;
}

// Rectangle outline, reproducing cv::rectangle(LINE_8, thickness > 0).
//
// OpenCV draws the four edges as thick lines and caps every line end with a
// filled disc (ThickLine -> FillConvexPoly + Circle, drawing.cpp), so the
// painted set is the union of
//   - four axis-aligned bands, each centred on its edge and extending +-k
//     pixels across, spanning only between the corners, and
//   - four filled discs of radius k centred on the corners.
// The discs are what round off the outer corner notches; a plain band union
// leaves them empty and a square corner extension overfills them. This model
// was checked pixel-exact against cv::rectangle for thickness 1..10 over every
// corner parity (/tmp/wm_poc/corner2.cpp, 6.4M pixels, 0 mismatches).
//
// Work item = (position along the edge / disc column, offset across the band /
// disc row, prim * 8 + sub), sub 0..3 = edges, sub 4..7 = corner discs.
__kernel void wm_outlines(__write_only image2d_t img, int iw, int ih,
                          __global const wm_prim *prims, int base) {
    const int id2 = get_global_id(2);
    const wm_prim p = prims[base + (id2 >> 3)];
    const int sub = id2 & 7;

    const int k = p.b.x;
    const int t = get_global_id(1);
    if (t > 2 * k)
        return;
    const int off = t - k;

    const int i = get_global_id(0);
    int x, y;
    if (sub >= 4) {                                 // corner disc
        const int d = i - k;
        if (i > 2 * k || d * d + off * off > k * k)
            return;
        x = ((sub & 1) ? p.a.z : p.a.x) + d;
        y = ((sub & 2) ? p.a.w : p.a.y) + off;
    } else if (sub < 2) {                           // top / bottom band
        x = p.a.x + i;
        if (x > p.a.z)
            return;
        y = (sub == 0 ? p.a.y : p.a.w) + off;
    } else {                                        // left / right band
        y = p.a.y + i;
        if (y > p.a.w)
            return;
        x = (sub == 2 ? p.a.x : p.a.z) + off;
    }

    if (x < 0 || y < 0 || x >= iw || y >= ih)
        return;
    write_imagef(img, (int2)(x, y), p.color);
}

// Filled rectangle, reproducing cv::rectangle(cv::FILLED): both corners
// inclusive.
__kernel void wm_fills(__write_only image2d_t img, int iw, int ih,
                       __global const wm_prim *prims, int base) {
    const wm_prim p = prims[base + get_global_id(2)];
    const int x = p.a.x + get_global_id(0);
    const int y = p.a.y + get_global_id(1);
    if (x > p.a.z || y > p.a.w)
        return;
    if (x < 0 || y < 0 || x >= iw || y >= ih)
        return;
    write_imagef(img, (int2)(x, y), p.color);
}

// One horizontal run [a.x, a.y] on row a.z. Lines, polygons and filled circles
// all reduce to runs: the CPU ports of OpenCV's FillConvexPoly / Line2 /
// LineIterator in renderer_gpu_raster.h decide *which* pixels OpenCV would
// paint, add_raster() coalesces them per row, and this kernel does the writing.
// Because every shape lands in one batch, the device-side draw order is the CPU
// renderer's prim order.
__kernel void wm_spans(__write_only image2d_t img, int iw, int ih,
                       __global const wm_prim *prims, int base) {
    const wm_prim p = prims[base + get_global_id(2)];
    const int x = p.a.x + (int)get_global_id(0);
    if (x > p.a.y)
        return;
    const int y = p.a.z;
    if (x < 0 || y < 0 || x >= iw || y >= ih)
        return;
    write_imagef(img, (int2)(x, y), p.color);
}

// Blit an 8-bit coverage mask (text rasterized on the CPU, segmentation
// masks thresholded on the CPU) from a linear atlas buffer. Non-zero
// coverage writes the flat prim colour, matching cv::putText with LINE_8.
__kernel void wm_masks(__write_only image2d_t img, int iw, int ih,
                       __global const uchar *atlas,
                       __global const wm_prim *prims, int base) {
    const wm_prim p = prims[base + get_global_id(2)];
    const int ix = get_global_id(0);
    const int iy = get_global_id(1);
    if (ix >= p.a.z || iy >= p.a.w)
        return;
    if (atlas[p.b.x + iy * p.b.y + ix] == 0)
        return;

    const int x = p.a.x + ix;
    const int y = p.a.y + iy;
    if (x < 0 || y < 0 || x >= iw || y >= ih)
        return;
    write_imagef(img, (int2)(x, y), p.color);
}

// Gaussian blur of one rectangle, reproducing cv::GaussianBlur() applied
// in-place to a submatrix with BORDER_DEFAULT - which is what
// RendererNV12::blur_rectangle does.
//
// Three properties of that call have to be reproduced and none of them is
// obvious:
//
//  - The bit-exact ufixedpoint16 path is skipped, because it needs
//    BORDER_ISOLATED or a non-submatrix (smooth.dispatch.cpp). What actually
//    runs is the generic sepFilter2D.
//  - The taps therefore read the plane's *real* pixels outside the rectangle,
//    and only reflect (BORDER_REFLECT_101) at the plane boundary.
//  - Which arithmetic sepFilter2D uses depends on the kernel: if every
//    coefficient times 256 is integral, it runs an integer fixed-point pass
//    (coefficients rounded to that integer, output (s + 1<<15) >> 16),
//    otherwise a float pass (output cvRound(s)). The host decides and passes
//    the answer in color.x. One float kernel serves both, because every
//    intermediate of the integer regime - row sums up to 256*255, column sums
//    up to 256*65280 - is exactly representable in binary32.
//
// Separable, so the cost is O(kw + kh) per pixel rather than O(kw * kh); split
// across two dispatches through a scratch buffer because a kernel may not both
// read and write one image (and this device has no __read_write image2d_t
// anyway). The row pass covers kh - 1 extra rows, which are the ones the column
// pass reaches above and below the rectangle.
//
// Every plane goes through the same code: the row pass reads whatever channels
// wm_vec is wide, so a CL_R plane simply carries a zero in .y.
__kernel void wm_blur_h(__read_only image2d_t img, int iw, int ih,
                        __global wm_vec *scratch,
                        __global const float *coeffs,
                        __global const wm_prim *prims, int base) {
    const wm_prim p = prims[base];
    const int rw = p.a.z, rh = p.a.w;
    const int kw = p.b.x, kh = p.b.y;

    const int ix = get_global_id(0);
    const int iy = get_global_id(1);
    if (ix >= rw || iy >= rh + kh - 1)
        return;

    const int sy = wm_reflect101(p.a.y + iy - (kh >> 1), ih);
    const int x0 = p.a.x + ix - (kw >> 1);
    __global const float *kx = coeffs + p.b.z;

    // OpenCV's generic RowFilter accumulates straight through the taps.
    wm_vec s = WM_ZERO;
    for (int k = 0; k < kw; ++k) {
        const float4 v = read_imagef(img, WM_SAMPLER, (int2)(wm_reflect101(x0 + k, iw), sy));
        s += kx[k] * round(WM_LOAD(v) * 255.f);
    }
    scratch[iy * rw + ix] = s;
}

__kernel void wm_blur_v(__write_only image2d_t img, int iw, int ih,
                        __global const wm_vec *scratch,
                        __global const float *coeffs,
                        __global const wm_prim *prims, int base) {
    const wm_prim p = prims[base];
    const int rw = p.a.z, rh = p.a.w;
    const int ay = p.b.y >> 1;

    const int ix = get_global_id(0);
    const int iy = get_global_id(1);
    if (ix >= rw || iy >= rh)
        return;

    // OpenCV's SymmColumnFilter pairs the taps around the centre one.
    __global const float *ky = coeffs + p.b.w + ay;
    __global const wm_vec *col = scratch + (iy + ay) * rw + ix;
    wm_vec s = ky[0] * col[0];
    for (int k = 1; k <= ay; ++k)
        s += ky[k] * (col[k * rw] + col[-k * rw]);

    if (p.color.x != 0.f) {
        // Integer fixed-point regime. OpenCV rounds a row of it two ways:
        // SymmColumnVec_32s8u divides by 1<<16 in float and rounds half to
        // even, and the elements it leaves at the end of the row go through the
        // scalar FixedPtCastEx, (s + 1<<15) >> 16, which rounds half up. Ties
        // are common right under a hard-edged rectangle, so the handover
        // column, measured host side, has to be honoured.
        s = ix < (int)p.color.y ? rint(s * (1.f / 65536.f)) : floor((s + 32768.f) * (1.f / 65536.f));
    } else {
        // Float regime: saturate_cast<uchar> either way, so one rounding.
        s = rint(s);
    }
    s = clamp(s, 0.f, 255.f) * (1.f / 255.f);

    const int x = p.a.x + ix, y = p.a.y + iy;
    if (x < 0 || y < 0 || x >= iw || y >= ih)
        return;
    write_imagef(img, (int2)(x, y), WM_STORE(s));
}

// Half-and-half blend of a flat colour under an 8-bit coverage bitmap,
// reproducing the addWeighted(0.5) + copyTo(mask) tail of
// RendererNV12::draw_instance_mask.
//
// Split in two for the same reason the blur is: the blend reads the pixel it
// then overwrites. The queue is in-order, so every gather has landed before the
// first scatter runs.
__kernel void wm_blend_gather(__read_only image2d_t img, int iw, int ih,
                              __global wm_vec *scratch,
                              __global const uchar *blob,
                              __global const wm_prim *prims, int base) {
    const wm_prim p = prims[base];
    const int ix = get_global_id(0);
    const int iy = get_global_id(1);
    if (ix >= p.a.z || iy >= p.a.w)
        return;
    if (blob[p.b.x + iy * p.b.y + ix] == 0)
        return;

    const int x = p.a.x + ix, y = p.a.y + iy;
    if (x < 0 || y < 0 || x >= iw || y >= ih)
        return;

    const float4 v = read_imagef(img, WM_SAMPLER, (int2)(x, y));
    const wm_vec src = round(WM_LOAD(v) * 255.f);
    const wm_vec col = round(WM_LOAD(p.color) * 255.f);
    const wm_vec out = clamp(rint(col * 0.5f + src * 0.5f), 0.f, 255.f);
    scratch[iy * p.a.z + ix] = out * (1.f / 255.f);
}

__kernel void wm_blend_scatter(__write_only image2d_t img, int iw, int ih,
                               __global const wm_vec *scratch,
                               __global const uchar *blob,
                               __global const wm_prim *prims, int base) {
    const wm_prim p = prims[base];
    const int ix = get_global_id(0);
    const int iy = get_global_id(1);
    if (ix >= p.a.z || iy >= p.a.w)
        return;
    if (blob[p.b.x + iy * p.b.y + ix] == 0)
        return;

    const int x = p.a.x + ix, y = p.a.y + iy;
    if (x < 0 || y < 0 || x >= iw || y >= ih)
        return;

    const wm_vec v = scratch[iy * p.a.z + ix];
    write_imagef(img, (int2)(x, y), WM_STORE(v));
}

)CLC";
// clang-format on

} // namespace renderer_gpu
