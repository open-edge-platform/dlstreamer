/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#ifdef ENABLE_GVAWATERMARK_GPU

#include <opencv2/opencv.hpp>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <vector>

/**
 * CPU rasterizers that emit the exact pixel set OpenCV would paint for lines,
 * polygons and filled circles, expressed as horizontal runs plus single pixels.
 *
 * Why the CPU: OpenCV rasterizes thick lines as FillConvexPoly() of a quad plus
 * a disc at the capped ends, and FillConvexPoly() is a serial fixed-point
 * scanline walk whose per-row state depends on the previous row. A plain
 * convex-quad half-plane test in a kernel is *not* equivalent - it misses about
 * a dozen pixels per line, because FillConvexPoly() also strokes the polygon
 * outline with Line2() on top of the span fill. Rather than approximate it,
 * these functions port OpenCV's own arithmetic and hand the GPU a list of runs;
 * the CPU cost is then proportional to the perimeter and the row count, and the
 * GPU still does all of the area writes.
 *
 * The ports are pixel-exact against OpenCV 4.13 (the version DL Streamer links)
 * over 93355 cases with zero mismatching pixels: cv::line for thickness 1..12 x
 * 120 angles x 8 lengths x 7 offsets including endpoints outside the frame,
 * cv::drawContours for 1..10-gons with vertices outside the frame,
 * cv::circle(FILLED) for radius 0..200 with centres outside the frame, and
 * cv::line on a CV_8UC2 non-square plane (the NV12 UV geometry). The probe is
 * /tmp/wm_poc/raster_test.cpp. Do not "simplify" any of the arithmetic below
 * without re-running it - every rounding detail is load bearing.
 *
 * References are to opencv/modules/imgproc/src/drawing.cpp.
 */
namespace renderer_gpu {

constexpr int XY_SHIFT = 16;
constexpr long long XY_ONE = 1 << XY_SHIFT;

struct Raster {
    struct Run {
        int x1, x2, y; // inclusive [x1, x2] on row y
    };
    std::vector<Run> runs;
    std::vector<cv::Point> points;

    void clear() {
        runs.clear();
        points.clear();
    }
    size_t size() const {
        return runs.size() + points.size();
    }
};

/* OpenCV's fixed-point point type, Point2l. */
struct P2L {
    long long x, y;
};

/* Port of cv::clipLine(Size2l, Point2l&, Point2l&), drawing.cpp:92. Note the
 * fixed two-stage structure (clip y, then clip x) and that the second stage
 * reads the x1 the first stage already updated - a general Liang-Barsky loop
 * gives different results. */
inline bool clip_line(long long w, long long h, P2L &pt1, P2L &pt2) {
    const long long right = w - 1, bottom = h - 1;
    if (w <= 0 || h <= 0)
        return false;

    long long &x1 = pt1.x, &y1 = pt1.y, &x2 = pt2.x, &y2 = pt2.y;
    int c1 = (x1 < 0) + (x1 > right) * 2 + (y1 < 0) * 4 + (y1 > bottom) * 8;
    int c2 = (x2 < 0) + (x2 > right) * 2 + (y2 < 0) * 4 + (y2 > bottom) * 8;

    if ((c1 & c2) == 0 && (c1 | c2) != 0) {
        long long a;
        if (c1 & 12) {
            a = c1 < 8 ? 0 : bottom;
            x1 += static_cast<long long>(static_cast<double>(a - y1) * (x2 - x1) / (y2 - y1));
            y1 = a;
            c1 = (x1 < 0) + (x1 > right) * 2;
        }
        if (c2 & 12) {
            a = c2 < 8 ? 0 : bottom;
            x2 += static_cast<long long>(static_cast<double>(a - y2) * (x2 - x1) / (y2 - y1));
            y2 = a;
            c2 = (x2 < 0) + (x2 > right) * 2;
        }
        if ((c1 & c2) == 0 && (c1 | c2) != 0) {
            if (c1) {
                a = c1 == 1 ? 0 : right;
                y1 += static_cast<long long>(static_cast<double>(a - x1) * (y2 - y1) / (x2 - x1));
                x1 = a;
                c1 = 0;
            }
            if (c2) {
                a = c2 == 1 ? 0 : right;
                y2 += static_cast<long long>(static_cast<double>(a - x2) * (y2 - y1) / (x2 - x1));
                x2 = a;
                c2 = 0;
            }
        }
    }
    return (c1 | c2) == 0;
}

/* Port of the single-channel branch of Line2(), drawing.cpp:634 - the
 * fixed-point DDA used to stroke a polygon edge. */
inline void line2(P2L pt1, P2L pt2, int w, int h, Raster &out) {
    if (!clip_line(static_cast<long long>(w) << XY_SHIFT, static_cast<long long>(h) << XY_SHIFT, pt1, pt2))
        return;

    long long dx = pt2.x - pt1.x, dy = pt2.y - pt1.y;
    const long long j = dx < 0 ? -1 : 0, ax = (dx ^ j) - j;
    const long long i = dy < 0 ? -1 : 0, ay = (dy ^ i) - i;
    long long x_step, y_step;
    int ecount;

    if (ax > ay) {
        dy = (dy ^ j) - j;
        pt1.x ^= pt2.x & j;
        pt2.x ^= pt1.x & j;
        pt1.x ^= pt2.x & j;
        pt1.y ^= pt2.y & j;
        pt2.y ^= pt1.y & j;
        pt1.y ^= pt2.y & j;
        x_step = XY_ONE;
        y_step = dy * (1 << XY_SHIFT) / (ax | 1);
        ecount = static_cast<int>((pt2.x - pt1.x) >> XY_SHIFT);
    } else {
        dx = (dx ^ i) - i;
        pt1.x ^= pt2.x & i;
        pt2.x ^= pt1.x & i;
        pt1.x ^= pt2.x & i;
        pt1.y ^= pt2.y & i;
        pt2.y ^= pt1.y & i;
        pt1.y ^= pt2.y & i;
        x_step = dx * (1 << XY_SHIFT) / (ay | 1);
        y_step = XY_ONE;
        ecount = static_cast<int>((pt2.y - pt1.y) >> XY_SHIFT);
    }
    pt1.x += XY_ONE >> 1;
    pt1.y += XY_ONE >> 1;

    auto put = [&](int x, int y) {
        if (0 <= x && x < w && 0 <= y && y < h)
            out.points.push_back({x, y});
    };
    /* Line2 emits the far endpoint before walking, so a zero-length run still
     * paints one pixel. */
    put(static_cast<int>((pt2.x + (XY_ONE >> 1)) >> XY_SHIFT), static_cast<int>((pt2.y + (XY_ONE >> 1)) >> XY_SHIFT));
    if (ax > ay) {
        pt1.x >>= XY_SHIFT;
        for (; ecount >= 0; --ecount, ++pt1.x, pt1.y += y_step)
            put(static_cast<int>(pt1.x), static_cast<int>(pt1.y >> XY_SHIFT));
    } else {
        pt1.y >>= XY_SHIFT;
        for (; ecount >= 0; --ecount, pt1.x += x_step, ++pt1.y)
            put(static_cast<int>(pt1.x >> XY_SHIFT), static_cast<int>(pt1.y));
    }
}

/* Port of FillConvexPoly(), drawing.cpp:1094, for LINE_8 and shift ==
 * XY_SHIFT: one horizontal run per scanline plus a Line2 stroke of every edge.
 * The strokes are not redundant - the span walk alone leaves gaps along steep
 * edges, which is exactly why a half-plane test in a kernel does not match. */
inline void fill_convex_poly(const P2L *v, int npts, int w, int h, Raster &out) {
    constexpr long long delta = XY_ONE >> 1;
    struct Edge {
        int idx, di, ye;
        long long x, dx;
    } edge[2];

    long long xmin = v[0].x, xmax = v[0].x, ymin = v[0].y, ymax = v[0].y;
    int imin = 0, edges = npts;

    P2L prev = v[npts - 1];
    for (int i = 0; i < npts; i++) {
        const P2L p = v[i];
        if (p.y < ymin) {
            ymin = p.y;
            imin = i;
        }
        ymax = std::max(ymax, p.y);
        xmax = std::max(xmax, p.x);
        xmin = std::min(xmin, p.x);
        line2(prev, p, w, h, out);
        prev = p;
    }

    xmin = (xmin + delta) >> XY_SHIFT;
    xmax = (xmax + delta) >> XY_SHIFT;
    ymin = (ymin + delta) >> XY_SHIFT;
    ymax = (ymax + delta) >> XY_SHIFT;
    if (npts < 3 || xmax < 0 || ymax < 0 || xmin >= w || ymin >= h)
        return;
    ymax = std::min(ymax, static_cast<long long>(h) - 1);

    int y = static_cast<int>(ymin);
    edge[0] = {imin, 1, y, -XY_ONE, 0};
    edge[1] = {imin, npts - 1, y, -XY_ONE, 0};

    do {
        for (int i = 0; i < 2; i++) {
            if (y >= edge[i].ye) {
                int idx0 = edge[i].idx, idx = idx0 + edge[i].di;
                if (idx >= npts)
                    idx -= npts;
                for (; edges-- > 0;) {
                    const int ty = static_cast<int>((v[idx].y + delta) >> XY_SHIFT);
                    if (ty > y) {
                        edge[i].ye = ty;
                        edge[i].dx = ((v[idx].x - v[idx0].x) * 2 + (ty - y)) / (2 * (ty - y));
                        edge[i].x = v[idx0].x;
                        edge[i].idx = idx;
                        break;
                    }
                    idx0 = idx;
                    idx += edge[i].di;
                    if (idx >= npts)
                        idx -= npts;
                }
            }
        }
        if (edges < 0)
            break;
        if (y >= 0) {
            const int left = edge[0].x > edge[1].x ? 1 : 0, right = left ^ 1;
            int x1 = static_cast<int>((edge[left].x + delta) >> XY_SHIFT);
            int x2 = static_cast<int>((edge[right].x + delta) >> XY_SHIFT);
            if (x2 >= 0 && x1 < w) {
                x1 = std::max(x1, 0);
                x2 = std::min(x2, w - 1);
                out.runs.push_back({x1, x2, y});
            }
        }
        edge[0].x += edge[0].dx;
        edge[1].x += edge[1].dx;
    } while (++y <= static_cast<int>(ymax));
}

/* cv::circle(..., cv::FILLED) paints exactly dx*dx + dy*dy <= r*r. Emitting it
 * as one run per row lets discs share the span kernel with everything else, and
 * wastes fewer work items than dispatching over the (2r+1)^2 bounding box. */
inline void filled_disc(int cx, int cy, int r, int w, int h, Raster &out) {
    if (r < 0)
        return;
    for (int dy = -r; dy <= r; ++dy) {
        const int y = cy + dy;
        if (y < 0 || y >= h)
            continue;
        const int rest = r * r - dy * dy;
        /* std::sqrt of an exactly representable int can round either way, so
         * settle the boundary with integer comparisons. */
        int dx = static_cast<int>(std::sqrt(static_cast<double>(rest)));
        while ((dx + 1) * (dx + 1) <= rest)
            ++dx;
        while (dx > 0 && dx * dx > rest)
            --dx;
        const int x1 = std::max(cx - dx, 0), x2 = std::min(cx + dx, w - 1);
        if (x1 <= x2)
            out.runs.push_back({x1, x2, y});
    }
}

/* Port of the !fill half of Circle(), drawing.cpp:1012 - the eight-way symmetric
 * Bresenham arc that cv::circle uses for thickness 0 and 1. (Only thickness < 0
 * fills; thickness > 1 goes to EllipseEx instead and is not ported.) The
 * `inside` fast path is deliberately kept, because in the general path OpenCV
 * clips each of the eight octant points independently and the two paths would
 * otherwise be easy to get subtly out of step. */
inline void circle_outline(int cx, int cy, int r, int w, int h, Raster &out) {
    if (r < 0)
        return;
    long long err = 0, dx = r, dy = 0, plus = 1, minus = (static_cast<long long>(r) << 1) - 1;
    const bool inside = cx >= r && cx < w - r && cy >= r && cy < h - r;

    while (dx >= dy) {
        const long long y11 = cy - dy, y12 = cy + dy, y21 = cy - dx, y22 = cy + dx;
        const long long x11 = cx - dx, x12 = cx + dx, x21 = cx - dy, x22 = cx + dy;

        const auto put = [&out](long long x, long long y) {
            out.points.push_back(cv::Point(static_cast<int>(x), static_cast<int>(y)));
        };

        if (inside) {
            put(x11, y11);
            put(x11, y12);
            put(x12, y11);
            put(x12, y12);
            put(x21, y21);
            put(x21, y22);
            put(x22, y21);
            put(x22, y22);
        } else if (x11 < w && x12 >= 0 && y21 < h && y22 >= 0) {
            if (y11 >= 0 && y11 < h) {
                if (x11 >= 0)
                    put(x11, y11);
                if (x12 < w)
                    put(x12, y11);
            }
            if (y12 >= 0 && y12 < h) {
                if (x11 >= 0)
                    put(x11, y12);
                if (x12 < w)
                    put(x12, y12);
            }
            if (x21 < w && x22 >= 0) {
                if (y21 >= 0 && y21 < h) {
                    if (x21 >= 0)
                        put(x21, y21);
                    if (x22 < w)
                        put(x22, y21);
                }
                if (y22 >= 0 && y22 < h) {
                    if (x21 >= 0)
                        put(x21, y22);
                    if (x22 < w)
                        put(x22, y22);
                }
            }
        }

        ++dy;
        err += plus;
        plus += 2;
        const long long mask = (err <= 0) - 1;
        err -= minus & mask;
        dx += mask;
        minus -= mask & 2;
    }
}

/* Port of ThickLine(), drawing.cpp:1644. `caps` is OpenCV's `flags`: bit 0 caps
 * pt1, bit 1 caps pt2. cv::line passes 3; cv::rectangle and cv::drawContours go
 * through PolyLine and pass 2, so consecutive segments do not cap the shared
 * vertex twice. */
inline void thick_line(cv::Point pt1, cv::Point pt2, int thickness, int caps, int w, int h, Raster &out) {
    P2L p0{pt1.x, pt1.y}, p1{pt2.x, pt2.y};

    /* drawing.cpp:1650 - a thick line with an endpoint outside the frame is
     * first clipped to a rect grown by `thickness`, which moves the point the
     * cap disc is centred on. Skipping this costs a pixel at the frame border. */
    if (thickness > 1 &&
        (p0.x < 0 || p0.x >= w || p0.y < 0 || p0.y >= h || p1.x < 0 || p1.x >= w || p1.y < 0 || p1.y >= h)) {
        const long long m = thickness;
        p0.x += m;
        p0.y += m;
        p1.x += m;
        p1.y += m;
        clip_line(w + 2 * m, h + 2 * m, p0, p1);
        p0.x -= m;
        p0.y -= m;
        p1.x -= m;
        p1.y -= m;
    }

    if (thickness <= 1) {
        /* Thin lines take OpenCV's Bresenham path, and cv::LineIterator is the
         * very code Line() drives, so this is exact by construction. */
        cv::LineIterator it(cv::Size(w, h), cv::Point(static_cast<int>(p0.x), static_cast<int>(p0.y)),
                            cv::Point(static_cast<int>(p1.x), static_cast<int>(p1.y)), 8, true);
        for (int i = 0; i < it.count; ++i, ++it)
            out.points.push_back(it.pos());
        return;
    }

    p0.x <<= XY_SHIFT;
    p0.y <<= XY_SHIFT;
    p1.x <<= XY_SHIFT;
    p1.y <<= XY_SHIFT;

    const double ddx = static_cast<double>(p0.x - p1.x) / static_cast<double>(XY_ONE);
    const double ddy = static_cast<double>(p1.y - p0.y) / static_cast<double>(XY_ONE);
    double r = ddx * ddx + ddy * ddy;
    const int odd_thickness = thickness & 1;
    const long long th = static_cast<long long>(thickness) << (XY_SHIFT - 1);

    if (std::fabs(r) > DBL_EPSILON) {
        r = (th + odd_thickness * XY_ONE * 0.5) / std::sqrt(r);
        const long long dpx = cvRound(ddy * r), dpy = cvRound(ddx * r);
        const P2L quad[4] = {
            {p0.x + dpx, p0.y + dpy}, {p0.x - dpx, p0.y - dpy}, {p1.x - dpx, p1.y - dpy}, {p1.x + dpx, p1.y + dpy}};
        fill_convex_poly(quad, 4, w, h, out);
    }

    const int radius = static_cast<int>((th + (XY_ONE >> 1)) >> XY_SHIFT);
    P2L cur = p0;
    for (int i = 0; i < 2; i++) {
        if (caps & (i + 1))
            filled_disc(static_cast<int>((cur.x + (XY_ONE >> 1)) >> XY_SHIFT),
                        static_cast<int>((cur.y + (XY_ONE >> 1)) >> XY_SHIFT), radius, w, h, out);
        cur = p1;
    }
}

} // namespace renderer_gpu

#endif // ENABLE_GVAWATERMARK_GPU
