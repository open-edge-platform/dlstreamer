/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#ifdef ENABLE_GVAWATERMARK_GPU

#include "color_converter.h"
#include "render_prim.h"
#include "renderer_gpu_raster.h"

#ifdef _WIN32
#ifndef GST_USE_UNSTABLE_API
#define GST_USE_UNSTABLE_API
#endif
#include <d3d11_4.h>
#include <gst/d3d11/gstd3d11.h>
#else
#include <va/va.h>
#endif

#ifndef CL_TARGET_OPENCL_VERSION
#define CL_TARGET_OPENCL_VERSION 300
#endif
#include <CL/cl.h>
#include <CL/cl_ext.h>
#ifdef _WIN32
#include <CL/cl_d3d11.h>
#else
#include <CL/cl_va_api_media_sharing_intel.h>
#endif

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace renderer_gpu {

#ifdef _WIN32
/* Frames are single NV12 D3D11 textures on this device, created shared
 * (D3D11_RESOURCE_MISC_SHARED), and drawn in place.
 *
 * Shared is the only kind OpenCL writes correctly: for any other, decoder
 * output included, the runtime goes through a staging copy that passes the
 * plane-encoded subresource straight to CopySubresourceRegion, so it copies the
 * wrong slice of a texture array and drops the UV plane of a single texture.
 * gvawatermarkimpl therefore copies every frame into such a texture first. */
using Device = GstD3D11Device *;
using Surface = ID3D11Texture2D *;
#else
/* Frames are NV12 VA surfaces of this display, drawn in place. */
using Device = VADisplay;
using Surface = VASurfaceID;
#endif

} // namespace renderer_gpu

/**
 * Rasterizes watermark primitives straight into the planes of a frame in video
 * memory with OpenCL: a VA surface on Linux, a D3D11 texture on Windows.
 *
 * On Linux the planes are imported zero-copy through
 * cl_intel_va_api_media_sharing, on Windows those of a shared D3D11 texture
 * through cl_khr_d3d11_sharing with cl_intel_d3d11_nv12_media_sharing (see
 * renderer_gpu::Surface). Either way there is no NV12->BGR->NV12 round trip and
 * no full-frame overlay: the drawing cost is proportional to the pixels the
 * primitives actually cover, not to the frame area. Text is still rasterized on
 * the CPU (Hershey vector fonts are not worth reimplementing on the GPU) and
 * uploaded as a small coverage mask.
 *
 * Pixel semantics are matched against RendererNV12, the CPU renderer the same
 * format would have used, because the CPU output is the golden reference.
 *
 * NV12 only. Every other format keeps using the CPU renderer.
 */
class RendererGPU {
  public:
    /* Returns nullptr when this platform cannot support the path (no sharing
     * extension, no OpenCL device for `device`, kernels fail to build). The
     * caller must then use the CPU renderer. */
    static std::unique_ptr<RendererGPU> create(renderer_gpu::Device device, int width, int height,
                                               std::shared_ptr<ColorConverter> color_converter);

    ~RendererGPU();

    RendererGPU(const RendererGPU &) = delete;
    RendererGPU &operator=(const RendererGPU &) = delete;

    /* Draws `prims` into `surface`. Returns false without touching the surface
     * when the primitive list contains something this renderer cannot draw yet;
     * the caller must then fall back to the CPU renderer for the frame.
     *
     * On Windows the caller must hold a GST_MAP_D3D11 write map of the texture,
     * and must not hold the device lock: the renderer takes it itself, only
     * around the calls that use the D3D11 immediate context. D3D11 work already
     * queued on the texture, such as the copy that filled it, is waited for. */
    bool draw_surface(renderer_gpu::Surface surface, std::vector<render::Prim> prims);

    /* Human readable reason for the last false returned by draw_surface(). */
    const std::string &last_unsupported() const {
        return _last_unsupported;
    }

  private:
    /* Mirrors `wm_prim` in renderer_gpu_kernels.h. */
    struct Prim {
        cl_int4 a;
        cl_int4 b;
        cl_float4 color;
        cl_int4 c;
    };
    static_assert(sizeof(Prim) == 64, "wm_prim layout must match the OpenCL struct");

    /* One entry of the CPU-rasterized coverage atlas. Text bitmaps are stable
     * across frames ("bottle 86%" typically survives dozens of frames), so
     * entries are keyed on the full set of rasterization parameters and reused,
     * which is what keeps the per-ROI cost down. */
    struct AtlasEntry {
        size_t offset;
        int width;
        int height;
        int origin_x; // add to text.org to get the blit destination
        int origin_y;
    };

    /* Process-wide, per-VADisplay OpenCL context and compiled program. Sharing
     * these across elements avoids one context and one program build per
     * stream; command queues and kernels stay per-instance because neither is
     * thread safe. */
    struct SharedContext;

    /* One kind of primitive, and with it one kernel. The low bit is the plane,
     * so `id & 1` selects it; plane 0 is Y and plane 1 is UV, and the two are
     * separate images and are scheduled independently. */
    enum BatchId {
        SPAN_P0, // rasterized lines, polygons and circles, as per-row runs
        SPAN_P1,
        OUTLINE_P0, // axis-aligned rectangle outlines
        OUTLINE_P1,
        FILL_P0, // filled rectangles (text backgrounds)
        FILL_P1,
        MASK_P0, // CPU-rasterized coverage masks (text)
        MASK_P1,
        COVER_P0, // text chroma blended by 2x2 coverage, two dispatches; UV only
        COVER_P1,
        BLUR_P0, // Gaussian blurs of rectangles out of each other's reach, two tiled dispatches
        BLUR_P1,
        BLEND_P0, // half-and-half blend of a segmentation mask, two dispatches
        BLEND_P1,
        BATCH_COUNT
    };

    /* One clEnqueueNDRangeKernel: primitives of a single kind whose pixels may
     * be written in any order.
     *
     * Getting this right is the whole reason the flattening is not just "one
     * batch per kind". Work items of one NDRange are unordered relative to each
     * other, so two records covering a pixel with *different* colours race, and
     * the frame is not even reproducible run to run. Overlapping draws therefore
     * have to be split across dispatches: the queue is in-order, so a later
     * group reliably paints over an earlier one, which is exactly the CPU
     * renderer's painter semantics.
     *
     * A group boundary is only forced where a primitive actually overlaps a
     * differently-coloured predecessor, so the ordinary frame - detections that
     * do not touch - still costs one dispatch per kind, as before. */
    struct Group {
        /* Conflict-test record for one primitive: its bounding box, inclusive,
         * plus what it paints there. `any_color` marks a record that has to be
         * treated as conflicting whatever the other colour is, which is what the
         * span groups use: they hold runs of many different colours under a
         * single box. */
        struct Box {
            int x0, y0, x1, y1;
            cl_float4 color;
            bool any_color;
        };

        BatchId id = SPAN_P0;
        std::vector<Prim> prims;
        std::vector<Box> boxes;
        int max_w = 0;         // global work size, dimension 0
        int max_h = 0;         // global work size, dimension 1
        int tiles[2] = {0, 0}; // blur: work-groups of the row pass and of the column pass
        int base = 0;          // offset into the concatenated device array
        size_t scratch = 0;    // wm_vec elements the group's blurs or text chroma use
        /* Blend holds exactly one primitive and goes out as a pair of
         * dispatches over the start of the scratch buffer, so nothing may be
         * merged into it and it is never reused as a scheduling target. */
        bool exclusive = false;
    };

    RendererGPU(std::shared_ptr<SharedContext> shared, std::shared_ptr<ColorConverter> color_converter, int width,
                int height);

    bool init();
    cl_command_queue create_queue(cl_int *err);

    /* Dimensions of one plane; plane 1 is the half-resolution chroma. */
    int plane_width(int plane) const {
        return plane == 0 ? _width : _width / 2;
    }
    int plane_height(int plane) const {
        return plane == 0 ? _height : _height / 2;
    }

    /* Per-frame primitive flattening. Returns false on an unsupported prim. */
    bool flatten(std::vector<render::Prim> &prims);
    bool flatten_rect(const render::Rect &rect);
    bool flatten_text(const render::Text &text);
    bool flatten_line(const render::Line &line);
    bool flatten_circle(const render::Circle &circle);
    bool flatten_polygon(const render::Polygon &polygon);
    bool flatten_blur(const render::Blur &blur);
    bool flatten_instance_mask(const render::InstanceSegmantationMask &mask);

    /* Picks the group this primitive belongs in and records its conflict box:
     * the first group running kernel `id` that comes after every group already
     * holding a conflicting primitive, or a fresh group at the end. Since
     * primitives arrive in the CPU renderer's draw order, this places each one
     * after everything it may not be allowed to race with, and nothing that
     * conflicts with it can ever be scheduled later. `scratch` is the slot the
     * primitive takes in the scratch buffer, which keeps a group from growing
     * past SCRATCH_GROUP_MAX. */
    Group &group_for(BatchId id, int x0, int y0, int x1, int y1, const cl_float4 &color, bool any_color,
                     size_t scratch = 0);
    /* Appends a group nothing else may join, for the instance mask blend: it
     * conflicts with any overlapping neighbour whatever its colour, and its two
     * dispatches have to stay adjacent in the queue. */
    Group &exclusive_group(BatchId id, int x0, int y0, int x1, int y1);

    void add_outline(BatchId id, int x0, int y0, int x1, int y1, int k, const cl_float4 &color);
    void add_fill(BatchId id, int x0, int y0, int x1, int y1, const cl_float4 &color);
    void add_mask(BatchId id, const AtlasEntry &entry, int dst_x, int dst_y, const cl_float4 &color);
    /* Blends `color` into the UV plane under the Y plane glyph `entry` placed at
     * luma position (dst_x, dst_y), by the share of each 2x2 block it covers. */
    void add_cover(BatchId id, const AtlasEntry &entry, int dst_x, int dst_y, const cl_float4 &color);
    /* Coalesces a rasterized pixel set into per-row runs and inserts them into
     * the pending per-row span lists in painter's order. Returns false once the
     * frame has produced more records than the per-frame budget, so a
     * pathological polygon falls back to the CPU instead of allocating without
     * bound. */
    bool add_raster(BatchId span_id, const renderer_gpu::Raster &raster, const cl_float4 &color);

    /* One painted interval [x1, x2] of one plane row. */
    struct SpanRec {
        int x1, x2;
        cl_float4 color;
    };
    /* Inserts [a, b] over whatever `row` already holds: the new interval wins,
     * so overlapped entries are trimmed or dropped. Keeps every entry of a row
     * disjoint from every other, which is what lets an arbitrary pile of
     * differently-coloured shapes go out as a single dispatch. */
    void insert_span(std::vector<SpanRec> &row, int a, int b, const cl_float4 &color);
    /* Moves the pending runs of one plane into a group, so that whatever is
     * scheduled next is ordered against them. Runs stay pending as long as
     * possible, because a pending run can still be clipped by a later shape and
     * a whole pile of shapes then costs a single dispatch. */
    void flush_spans(int plane);
    /* Flushes only if the pending runs are under the given box, which is the
     * only case where the next primitive has to be ordered against them. */
    void flush_spans_overlapping(int plane, int x0, int y0, int x1, int y1);

    /* The two forms of a Gaussian kernel of a given length: the float
     * coefficients cv::getGaussianKernel returns, and, when OpenCV would accept
     * them as bit-exact, the same coefficients scaled by 256 and rounded. Which
     * pair sepFilter2D uses is decided per blur, because it takes the row and
     * the column kernel together. */
    struct CoeffEntry {
        size_t float_offset;
        size_t int_offset;
        bool int_valid;
    };
    const CoeffEntry *gaussian_coeffs(int ksize);

    /* In the integer fixed-point regime OpenCV's column filter rounds two
     * different ways within a single row: SymmColumnVec_32s8u accumulates in
     * float and rounds half to even, and whatever it leaves at the end of the
     * row falls to the scalar (s + 1<<15) >> 16, which rounds half up. The two
     * disagree on exact .5 ties, which a hard-edged rectangle under a blur hits
     * readily, so the handover column has to be reproduced. It is
     * VTraits<v_uint8>::vlanes() dependent, i.e. it varies with the SIMD width
     * OpenCV dispatched to, so it is measured rather than assumed. */
    void calibrate_blur_rounding();
    /* In the float regime the same split, and within the scalar tail the split
     * between its 4-wide unrolled loop and the 1-wide rest, decide whether a
     * multiply-add is fused. That is down to how OpenCV was compiled - FMA3 in
     * the vector code, contraction in the scalar code, which a compiler may
     * apply to one loop and not to another - so it is measured for each part
     * of the row in each pass. Needs _blur_vec_lanes. */
    void calibrate_blur_fma();
    /* Elements (not pixels) of a row of `n` that the vectorized path takes. */
    int blur_vector_elements(int n) const;
    /* The same in the float regime, which has no half-width step. */
    int blur_float_vector_elements(int n) const;
    /* Float regime: elements of a row of `n` up to the end of the scalar
     * tail's 4-wide loop. */
    int blur_unrolled_elements(int n) const;

    /* One rectangle of one plane, blurred in place. `cn` is the plane's channel
     * count, which the rounding handover is expressed in. */
    void add_blur(BatchId id, const cv::Rect &rect, int cn, int kw, int kh, const CoeffEntry &cx, const CoeffEntry &cy,
                  bool integer_regime);
    /* One rectangle of one plane, blended half-and-half with `color` wherever
     * `bitmap` (CV_8UC1, rectangle sized) is non-zero. */
    bool add_blend(BatchId id, const cv::Rect &rect, const cv::Mat &bitmap, const cl_float4 &color);

    /* The label's glyph over the same region rasterize_text() in
     * renderer_cpu.cpp uses, which is clipped to the frame and aligned to 2x2
     * blocks. Its origin is relative to text.org; an entry of width 0 means
     * the label is off the frame. */
    const AtlasEntry *rasterize_text(const render::Text &text);
    const AtlasEntry _off_frame{};

    bool sync_atlas();
    bool sync_coeffs();
    bool sync_blob();
    bool sync_scratch();
    bool sync_prims();
    bool get_planes(renderer_gpu::Surface surface, cl_mem planes[2]);
    /* Imports one plane of `surface` as an OpenCL image. */
    cl_mem import_plane(renderer_gpu::Surface surface, int plane, cl_int *err);
    bool enqueue(const Group &group, cl_mem image, int image_w, int image_h);

    static cl_float4 plane_color_y(const cv::Scalar &yuv);
    static cl_float4 plane_color_uv(const cv::Scalar &yuv);

    std::shared_ptr<SharedContext> _shared;
    std::shared_ptr<ColorConverter> _color_converter;

    const int _width;
    const int _height;

    cl_command_queue _queue = nullptr;
    cl_kernel _k_outlines = nullptr;
    cl_kernel _k_fills = nullptr;
    cl_kernel _k_masks = nullptr;
    cl_kernel _k_spans = nullptr;
    cl_kernel _k_blur_h = nullptr;
    cl_kernel _k_blur_v = nullptr;
    cl_kernel _k_blend_gather = nullptr;
    cl_kernel _k_blend_scatter = nullptr;
    cl_kernel _k_cover_gather = nullptr;
    cl_kernel _k_cover_scatter = nullptr;

    /* Text colour on a background box, in the frame's colour space. */
    cv::Scalar _white;

    /* Scratch for the CPU rasterizers and for the run coalescing in
     * add_raster(), reused across prims and frames so the per-frame path does
     * not allocate. */
    struct Span {
        int y, x1, x2;
    };
    renderer_gpu::Raster _raster;
    std::vector<Span> _merge;

    /* Painter's-order accumulator for the rasterized shapes, one entry per plane
     * row, indexed [plane][row]. Sized once and only cleared per frame, so the
     * row vectors keep their capacity. _span_pending counts the entries
     * currently held, which is what lets the reset and the flush walk be skipped
     * on the ordinary frame that draws no shapes; _span_box is their union,
     * used to decide whether a later primitive lands on top of them. */
    std::array<std::vector<std::vector<SpanRec>>, 2> _span_rows;
    std::array<size_t, 2> _span_pending = {0, 0};
    std::array<std::array<int, 4>, 2> _span_box{};
    size_t _span_records = 0; // pending plus already flushed, for the budget

    /* Frames cycle through a fixed pool, so the imported plane images are
     * cached per surface instead of reimported every frame, except where
     * SharedContext::cache_imports forbids it and only the latest frame's
     * import is kept. */
    std::unordered_map<renderer_gpu::Surface, std::array<cl_mem, 2>> _plane_cache;

#ifdef _WIN32
    /* Signalled on the immediate context behind the D3D11 work queued on the
     * frame. The OpenCL context leaves D3D11 synchronization to us, and waiting
     * on this fence, unlike the driver's own wait in acquire, does not hold the
     * device lock while the GPU works, which would stall every other stream and
     * the decoder. */
    ID3D11DeviceContext4 *_d3d11_context = nullptr;
    ID3D11Fence *_d3d11_fence = nullptr;
    UINT64 _d3d11_fence_value = 0;
    HANDLE _d3d11_fence_event = nullptr;
#endif

    /* Dispatch schedule, per plane, in order. Groups are reused across frames
     * for their storage; _group_count says how many of them the current frame
     * actually uses. */
    std::array<std::vector<Group>, 2> _groups;
    std::array<size_t, 2> _group_count = {0, 0};
    std::vector<Prim> _prims; // all groups concatenated, as uploaded
    cl_mem _prim_buffer = nullptr;
    size_t _prim_buffer_capacity = 0;

    std::unordered_map<std::string, AtlasEntry> _atlas_index;
    std::vector<uint8_t> _atlas_host;
    cl_mem _atlas_buffer = nullptr;
    size_t _atlas_buffer_capacity = 0;
    size_t _atlas_uploaded = 0;

    /* Gaussian coefficients, keyed on kernel length. Only a handful of lengths
     * occur in a stream and they recur frame after frame, so this is populated
     * once and then only read. */
    std::unordered_map<int, CoeffEntry> _coeff_index;
    std::vector<float> _coeff_host;
    cl_mem _coeff_buffer = nullptr;
    size_t _coeff_buffer_capacity = 0;
    size_t _coeff_uploaded = 0;

    /* Measured by calibrate_blur_rounding(): the number of uchars OpenCV's
     * vectorized column filter consumes per step, or 0 if this build has no
     * vector path at all. */
    int _blur_vec_lanes = 0;
    /* Measured by calibrate_blur_fma(): which parts of a row OpenCV's float
     * filters fuse their multiply-adds in. Bits 0-2 are the row pass's vector
     * code, the 4-wide loop of its scalar tail and the 1-wide rest; bits 3-5
     * the same for the column pass. */
    int _blur_fma = 0x3f;

    /* Segmentation coverage bitmaps. Unlike glyphs these are different every
     * frame, so they get their own buffer and are rebuilt from scratch rather
     * than evicting the cached text. */
    std::vector<uint8_t> _blob_host;
    cl_mem _blob_buffer = nullptr;
    size_t _blob_buffer_capacity = 0;

    /* Staging for the read-modify-write primitives: the blur's row-pass output
     * and the blend's read-back destination. Each of them is a pair of adjacent
     * dispatches on an in-order queue, so a single buffer sized to the largest
     * of them serves the whole frame. One element is the kernels' `wm_vec`, a
     * float2, which is as wide as either NV12 plane has channels to carry. */
    cl_mem _scratch_buffer = nullptr;
    size_t _scratch_buffer_capacity = 0; // in wm_vec elements
    size_t _scratch_needed = 0;

    std::string _last_unsupported;
};

#endif // ENABLE_GVAWATERMARK_GPU
