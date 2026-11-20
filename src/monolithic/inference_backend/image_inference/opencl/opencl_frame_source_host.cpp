/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

// Frames in system memory: each frame is uploaded once into OpenCL images (a buffer for 3-byte pixels), which its
// ROIs share. The images come from a pool and go back to it when the kernels that read them have completed.

#include "opencl_frame_source.h"
#include "opencl_utils.h"

#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace InferenceBackend {

namespace {

using UploadSet = std::array<cl_mem, 3>;

// Upload memory objects of one kind and size, recycled.
class UploadPool {
  public:
    using Key = std::tuple<int, uint32_t, uint32_t, uint32_t>; // format, width, height, stride

    explicit UploadPool(cl_context context) : context_(context) {
        opencl::Check(clRetainContext(context_), "clRetainContext");
    }

    ~UploadPool() {
        for (auto &entry : free_)
            for (const UploadSet &set : entry.second)
                for (cl_mem mem : set)
                    if (mem)
                        clReleaseMemObject(mem);
        clReleaseContext(context_);
    }

    UploadPool(const UploadPool &) = delete;
    UploadPool &operator=(const UploadPool &) = delete;

    UploadSet Acquire(const Key &key) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = free_.find(key);
            if (it != free_.end() && !it->second.empty()) {
                UploadSet set = it->second.back();
                it->second.pop_back();
                return set;
            }
        }
        return Create(key);
    }

    void Release(const Key &key, const UploadSet &set) {
        std::lock_guard<std::mutex> lock(mutex_);
        free_[key].push_back(set);
    }

  private:
    cl_mem CreateImage(cl_channel_order order, uint32_t width, uint32_t height) {
        cl_image_format format = {order, CL_UNORM_INT8};
        cl_image_desc desc = {};
        desc.image_type = CL_MEM_OBJECT_IMAGE2D;
        desc.image_width = width;
        desc.image_height = height;
        cl_int err = CL_SUCCESS;
        cl_mem image = clCreateImage(context_, CL_MEM_READ_ONLY, &format, &desc, nullptr, &err);
        opencl::Check(err, "clCreateImage");
        return image;
    }

    UploadSet Create(const Key &key) {
        const auto [fourcc, width, height, stride] = key;
        const uint32_t chroma_w = (width + 1) / 2, chroma_h = (height + 1) / 2;
        UploadSet set = {nullptr, nullptr, nullptr};
        try {
            switch (fourcc) {
            case FourCC::FOURCC_NV12:
                set[0] = CreateImage(CL_R, width, height);
                set[1] = CreateImage(CL_RG, chroma_w, chroma_h);
                break;
            case FourCC::FOURCC_I420:
                set[0] = CreateImage(CL_R, width, height);
                set[1] = CreateImage(CL_R, chroma_w, chroma_h);
                set[2] = CreateImage(CL_R, chroma_w, chroma_h);
                break;
            case FourCC::FOURCC_BGRX:
            case FourCC::FOURCC_BGRA:
            case FourCC::FOURCC_RGBX:
            case FourCC::FOURCC_RGBA:
                set[0] = CreateImage(CL_RGBA, width, height);
                break;
            case FourCC::FOURCC_BGR:
            case FourCC::FOURCC_RGB: {
                cl_int err = CL_SUCCESS;
                set[0] =
                    clCreateBuffer(context_, CL_MEM_READ_ONLY, static_cast<size_t>(stride) * height, nullptr, &err);
                opencl::Check(err, "clCreateBuffer");
                break;
            }
            default:
                throw std::invalid_argument("unsupported system memory format " + std::to_string(fourcc));
            }
        } catch (...) {
            for (cl_mem mem : set)
                if (mem)
                    clReleaseMemObject(mem);
            throw;
        }
        return set;
    }

    cl_context context_;
    std::mutex mutex_;
    std::map<Key, std::vector<UploadSet>> free_;
};

void WriteImage(cl_command_queue queue, cl_mem image, uint32_t width, uint32_t height, uint32_t stride,
                const uint8_t *data) {
    if (!data)
        throw std::invalid_argument("OpenCL pre-processing: system memory frame without a plane");
    const size_t origin[3] = {0, 0, 0};
    const size_t region[3] = {width, height, 1};
    // Not blocking: the frame stays mapped until its inference is done, which is after these writes.
    opencl::Check(clEnqueueWriteImage(queue, image, CL_FALSE, origin, region, stride, 0, data, 0, nullptr, nullptr),
                  "clEnqueueWriteImage");
}

class HostFrameSource : public OpenclFrameSource {
  public:
    explicit HostFrameSource(cl_context context) : pool_(std::make_shared<UploadPool>(context)) {
    }

    OpenclSourcePlanesPtr GetPlanes(const ImagePtr &image, cl_command_queue queue) override {
        if (!image || image->type != MemoryType::SYSTEM)
            throw std::invalid_argument("OpenCL pre-processing: expected a system memory frame");

        // The ROIs of a frame come one after another and share the image, whose rect changes per ROI.
        if (auto last = last_image_.lock(); last && last.get() == image.get() && last_planes_)
            return last_planes_;

        const Image &src = *image;
        const uint32_t w = src.width, h = src.height;
        const uint32_t chroma_w = (w + 1) / 2, chroma_h = (h + 1) / 2;
        uint32_t stride = 0;
        if (src.format == FourCC::FOURCC_BGR || src.format == FourCC::FOURCC_RGB)
            stride = src.stride[0];
        const UploadPool::Key key{src.format, w, h, stride};
        const UploadSet set = pool_->Acquire(key);

        auto planes = std::make_shared<OpenclSourcePlanes>();
        // The planes hold their own references; the set goes back to the pool once the planes are released.
        for (cl_mem mem : set)
            if (mem)
                clRetainMemObject(mem);
        planes->planes = set;
        std::weak_ptr<UploadPool> pool = pool_;
        planes->on_destroy = [pool, key, set]() {
            if (auto p = pool.lock())
                p->Release(key, set);
            else
                for (cl_mem mem : set)
                    if (mem)
                        clReleaseMemObject(mem);
        };

        switch (src.format) {
        case FourCC::FOURCC_NV12:
            planes->layout = OpenclSourcePlanes::Layout::NV12;
            planes->num_planes = 2;
            WriteImage(queue, set[0], w, h, src.stride[0], src.planes[0]);
            WriteImage(queue, set[1], chroma_w, chroma_h, src.stride[1], src.planes[1]);
            break;
        case FourCC::FOURCC_I420:
            planes->layout = OpenclSourcePlanes::Layout::I420;
            planes->num_planes = 3;
            WriteImage(queue, set[0], w, h, src.stride[0], src.planes[0]);
            WriteImage(queue, set[1], chroma_w, chroma_h, src.stride[1], src.planes[1]);
            WriteImage(queue, set[2], chroma_w, chroma_h, src.stride[2], src.planes[2]);
            break;
        case FourCC::FOURCC_BGRX:
        case FourCC::FOURCC_BGRA:
        case FourCC::FOURCC_RGBX:
        case FourCC::FOURCC_RGBA:
            // Read as RGBA, i.e. in memory order.
            planes->layout = OpenclSourcePlanes::Layout::RGBX;
            planes->num_planes = 1;
            planes->bgr_order = src.format == FourCC::FOURCC_BGRX || src.format == FourCC::FOURCC_BGRA;
            WriteImage(queue, set[0], w, h, src.stride[0], src.planes[0]);
            break;
        case FourCC::FOURCC_BGR:
        case FourCC::FOURCC_RGB:
            planes->layout = OpenclSourcePlanes::Layout::RGB24;
            planes->num_planes = 1;
            planes->stride = stride;
            planes->bgr_order = src.format == FourCC::FOURCC_BGR;
            if (!src.planes[0] || stride < w * 3)
                throw std::invalid_argument("OpenCL pre-processing: invalid packed RGB frame");
            opencl::Check(clEnqueueWriteBuffer(queue, set[0], CL_FALSE, 0,
                                               static_cast<size_t>(stride) * (h - 1) + w * 3, src.planes[0], 0, nullptr,
                                               nullptr),
                          "clEnqueueWriteBuffer");
            break;
        default:
            throw std::invalid_argument("OpenCL pre-processing: unsupported system memory format " +
                                        std::to_string(src.format));
        }

        last_image_ = image;
        last_planes_ = planes;
        return planes;
    }

  private:
    std::shared_ptr<UploadPool> pool_;
    // The last frame uploaded. Only a weak reference: the frame must be released when its inference is done.
    std::weak_ptr<Image> last_image_;
    OpenclSourcePlanesPtr last_planes_;
};

} // namespace

std::unique_ptr<OpenclFrameSource> CreateOpenclHostFrameSource(cl_context context) {
    return std::make_unique<HostFrameSource>(context);
}

} // namespace InferenceBackend
