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

#include <va/va.h>

#ifndef CL_TARGET_OPENCL_VERSION
#define CL_TARGET_OPENCL_VERSION 300
#endif
#include <CL/cl.h>
#include <CL/cl_va_api_media_sharing_intel.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

/**
 * Rasterizes watermark primitives straight into the planes of a VA surface
 * with OpenCL.
 *
 * The planes are imported zero-copy through cl_intel_va_api_media_sharing, so
 * there is no NV12->BGR->NV12 round trip and no full-frame overlay: the cost is
 * proportional to the pixels the primitives actually cover, not to the frame
 * area. Text is still rasterized on the CPU (Hershey vector fonts are not worth
 * reimplementing on the GPU) and uploaded as a small coverage mask.
 *
 * Pixel semantics are matched against RendererNV12, the CPU renderer the same
 * format would have used, because the CPU output is the golden reference.
 *
 * NV12 only. Every other format keeps using the CPU renderer.
 */
class RendererGPU {
  public:
    /* Returns nullptr when this platform cannot support the path (no
     * cl_intel_va_api_media_sharing, no device for the VADisplay, kernels fail
     * to build). The caller must then use the CPU renderer. */
    static std::unique_ptr<RendererGPU> create(VADisplay display, int width, int height,
                                               std::shared_ptr<ColorConverter> color_converter);

    ~RendererGPU();

    RendererGPU(const RendererGPU &) = delete;
    RendererGPU &operator=(const RendererGPU &) = delete;

    /* Draws `prims` into `surface`. Returns false without touching the surface
     * when the primitive list contains something this renderer cannot draw yet;
     * the caller must then fall back to the CPU renderer for the frame. */
    bool draw_surface(VASurfaceID surface, std::vector<render::Prim> prims);

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
    };
    static_assert(sizeof(Prim) == 48, "wm_prim layout must match the OpenCL struct");

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
        BLUR_P0, // Gaussian blur of one rectangle, two dispatches
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
        int max_w = 0; // global work size, dimension 0
        int max_h = 0; // global work size, dimension 1
        int base = 0;  // offset into the concatenated device array
        /* Blur and blend hold exactly one primitive and go out as a pair of
         * dispatches sharing one scratch buffer, so nothing may be merged into
         * them and they are never reused as a scheduling target. */
        bool exclusive = false;
    };

    RendererGPU(std::shared_ptr<SharedContext> shared, std::shared_ptr<ColorConverter> color_converter, int width,
                int height);

    bool init();

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
     * conflicts with it can ever be scheduled later. */
    Group &group_for(BatchId id, int x0, int y0, int x1, int y1, const cl_float4 &color, bool any_color);
    /* Appends a group nothing else may join. Read-modify-write primitives need
     * this: they conflict with any overlapping neighbour whatever its colour,
     * and their two dispatches have to stay adjacent in the queue. */
    Group &exclusive_group(BatchId id, int x0, int y0, int x1, int y1);

    void add_outline(BatchId id, int x0, int y0, int x1, int y1, int k, const cl_float4 &color);
    void add_fill(BatchId id, int x0, int y0, int x1, int y1, const cl_float4 &color);
    void add_mask(BatchId id, const AtlasEntry &entry, int dst_x, int dst_y, const cl_float4 &color);
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
    /* Elements (not pixels) of a row of `n` that the vectorized path takes. */
    int blur_vector_elements(int n) const;

    /* One rectangle of one plane, blurred in place. `cn` is the plane's channel
     * count, which the rounding handover is expressed in. */
    void add_blur(BatchId id, const cv::Rect &rect, int cn, int kw, int kh, const CoeffEntry &cx, const CoeffEntry &cy,
                  bool integer_regime);
    /* One rectangle of one plane, blended half-and-half with `color` wherever
     * `bitmap` (CV_8UC1, rectangle sized) is non-zero. */
    bool add_blend(BatchId id, const cv::Rect &rect, const cv::Mat &bitmap, const cl_float4 &color);

    const AtlasEntry *rasterize_text(const std::string &text, int fonttype, double fontscale, int thick);

    bool sync_atlas();
    bool sync_coeffs();
    bool sync_blob();
    bool sync_scratch();
    bool sync_prims();
    bool get_planes(VASurfaceID surface, cl_mem planes[2]);
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

    /* VA surfaces cycle through a fixed pool, so the imported plane images are
     * cached per surface id instead of reimported every frame. */
    std::unordered_map<VASurfaceID, std::array<cl_mem, 2>> _plane_cache;

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
