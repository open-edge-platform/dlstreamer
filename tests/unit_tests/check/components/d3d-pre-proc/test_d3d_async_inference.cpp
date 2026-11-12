/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

// Tests for ImageInferenceAsyncD3D11/D3D12, the wrappers that pre-process frames on the GPU
// and hand them to the inference on a worker thread: the images they produce, the converter
// cache, frames from another device, and the error and teardown paths. The inference is a fake
// that inspects the images it is given.

#include "image_inference_async_d3d11.h"
#include "image_inference_async_d3d12.h"

#include "dlstreamer/d3d11/context.h"
#include "dlstreamer/d3d12/context.h"
#include "inference_backend/image.h"

#include "utils.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace InferenceBackend;
using namespace d3d_test;

namespace {

using Roi = InferenceBackend::Rectangle<uint32_t>;

constexpr uint32_t MODEL_WIDTH = 64;
constexpr uint32_t MODEL_HEIGHT = 64;
// Bilinear sampling blends the quadrant edges with their neighbours
constexpr uint32_t inset = 3;

struct TestFrame : ImageInference::IFrameBase {
    ImagePtr image;
    void SetImage(ImagePtr img) override {
        image = std::move(img);
    }
    ImagePtr GetImage() const override {
        return image;
    }
};

BgraImage ReadSystemImage(const Image &image) {
    BgraImage out;
    out.width = image.width;
    out.height = image.height;
    out.pixels.resize(static_cast<size_t>(image.width) * image.height);
    for (uint32_t y = 0; y < image.height; y++)
        std::memcpy(&out.pixels[static_cast<size_t>(y) * image.width], image.planes[0] + y * image.stride[0],
                    image.width * sizeof(Bgra));
    return out;
}

// Stands in for OpenVINOImageInference. SubmitImage runs on the wrapper's worker thread.
class FakeInference : public ImageInference {
  public:
    MemoryType memory_type = MemoryType::SYSTEM;
    size_t nireq = 2;
    // Called with every submitted frame. By default the image is read back and released at once.
    std::function<void(IFrameBase::Ptr)> on_submit;
    // Frames that Flush() completes (released), as the real inference does with its requests
    std::vector<IFrameBase::Ptr> held;
    std::function<BgraImage(const Image &)> read_target = ReadSystemImage;

    std::mutex mutex;
    std::vector<BgraImage> results;
    std::vector<int> result_formats;
    std::vector<MemoryType> result_types;

    void SubmitImage(IFrameBase::Ptr frame, const std::map<std::string, std::shared_ptr<InputLayerDesc>> &) override {
        if (on_submit) {
            on_submit(frame);
            return;
        }
        Record(*frame->GetImage());
        frame->SetImage(nullptr);
    }

    void Record(const Image &image) {
        BgraImage pixels = read_target(image);
        std::lock_guard lk(mutex);
        results.push_back(std::move(pixels));
        result_formats.push_back(image.format);
        result_types.push_back(image.type);
    }

    size_t ResultCount() {
        std::lock_guard lk(mutex);
        return results.size();
    }

    const std::string &GetModelName() const override {
        return name;
    }
    size_t GetBatchSize() const override {
        return 1;
    }
    size_t GetNireq() const override {
        return nireq;
    }
    void GetModelImageInputInfo(size_t &width, size_t &height, size_t &batch_size, int &format,
                                int &memory) const override {
        width = MODEL_WIDTH;
        height = MODEL_HEIGHT;
        batch_size = 1;
        format = FourCC::FOURCC_BGRA;
        memory = static_cast<int>(memory_type);
    }
    std::map<std::string, std::vector<size_t>> GetModelInputsInfo() const override {
        return {};
    }
    std::map<std::string, std::vector<size_t>> GetModelOutputsInfo() const override {
        return {};
    }
    std::map<std::string, GstStructure *> GetModelInfoPostproc() const override {
        return {};
    }
    bool IsQueueFull() override {
        return false;
    }
    void Flush() override {
        std::lock_guard lk(mutex);
        for (auto &frame : held)
            frame->SetImage(nullptr);
        held.clear();
    }
    void Close() override {
    }

  private:
    std::string name = "fake";
};

// A hung wrapper must fail the test run instead of blocking it
template <typename Fn>
void WithTimeout(Fn fn, int seconds = 60) {
    auto done = std::async(std::launch::async, fn);
    if (done.wait_for(std::chrono::seconds(seconds)) != std::future_status::ready) {
        std::fprintf(stderr, "Timed out after %d s: the wrapper is hung\n", seconds);
        std::abort();
    }
    done.get();
}

struct D3D11Backend {
    using Wrapper = ImageInferenceAsyncD3D11;
    using DevicePtr = D3D11DevicePtr;
    using Device = GstD3D11Device;
    static constexpr const char *name = "D3D11";
    static constexpr MemoryType surface_memory = MemoryType::D3D11;

    static DevicePtr CreateDevice() {
        return CreateD3D11Device();
    }
    static dlstreamer::ContextPtr Context(Device *device) {
        return std::make_shared<dlstreamer::D3D11Context>(device);
    }
    static BufferPtr MakeSource(Device *device, uint32_t width, uint32_t height, const PixelFn &pixel) {
        auto buffer = AllocD3D11Texture(device, width, height, DXGI_FORMAT_B8G8R8A8_UNORM, D3D11_BIND_SHADER_RESOURCE);
        FillD3D11(buffer.get(), pixel);
        return buffer;
    }
    // The image of a decoded frame, as BufferToImageMapper makes it. `queried` is the device the
    // upstream device query returned, which can differ from the one the texture belongs to.
    static ImagePtr SourceImage(GstBuffer *buffer, Device *queried) {
        auto image = std::make_shared<Image>();
        image->type = MemoryType::D3D11;
        image->d3d11_texture = D3D11Texture(buffer);
        image->d3d11_subresource_index = 0;
        image->gst_d3d11_device = queried;
        image->gst_d3d11_memory = gst_buffer_peek_memory(buffer, 0);
        return image;
    }
    // The converted texture of a surface sharing image, on the inference device
    static BgraImage ReadSurfaceImage(Device *device, const Image &image) {
        GstMemory *mem = gst_d3d11_allocator_alloc_wrapped(nullptr, device, image.d3d11_texture, 0, nullptr, nullptr);
        if (!mem)
            throw std::runtime_error("failed to wrap the converted texture");
        BufferPtr buffer(gst_buffer_new());
        gst_buffer_append_memory(buffer.get(), mem);
        return ReadD3D11(buffer.get());
    }
};

struct D3D12Backend {
    using Wrapper = ImageInferenceAsyncD3D12;
    using DevicePtr = D3D12DevicePtr;
    using Device = GstD3D12Device;
    static constexpr const char *name = "D3D12";
    static constexpr MemoryType surface_memory = MemoryType::D3D12;

    static DevicePtr CreateDevice() {
        return CreateD3D12Device();
    }
    static dlstreamer::ContextPtr Context(Device *device) {
        return std::make_shared<dlstreamer::D3D12Context>(device);
    }
    static BufferPtr MakeSource(Device *device, uint32_t width, uint32_t height, const PixelFn &pixel) {
        auto buffer = AllocD3D12Texture(device, width, height, DXGI_FORMAT_B8G8R8A8_UNORM, D3D12_RESOURCE_FLAG_NONE);
        FillD3D12(buffer.get(), pixel);
        return buffer;
    }
    static ImagePtr SourceImage(GstBuffer *buffer, Device *queried) {
        auto image = std::make_shared<Image>();
        image->type = MemoryType::D3D12;
        image->d3d12_resource = D3D12Resource(buffer);
        image->d3d12_subresource_index = 0;
        image->gst_d3d12_device = queried;
        image->gst_d3d12_memory = gst_buffer_peek_memory(buffer, 0);
        return image;
    }
    static BgraImage ReadSurfaceImage(Device *, const Image &image) {
        // Mapping the memory waits for the fence the converter attached to it
        BufferPtr buffer(gst_buffer_new());
        gst_buffer_append_memory(buffer.get(), gst_memory_ref(static_cast<GstMemory *>(image.gst_d3d12_memory)));
        return ReadD3D12(buffer.get());
    }
};

template <typename Backend>
class D3DAsyncInferenceTest : public testing::Test {
  protected:
    typename Backend::DevicePtr device;
    std::shared_ptr<FakeInference> inference = std::make_shared<FakeInference>();
    std::mutex failed_mutex;
    std::vector<ImageInference::IFrameBase::Ptr> failed;
    // Source buffers stay alive until the test ends: the GPU may still read them after SubmitImage
    std::vector<BufferPtr> sources;

    void SetUp() override {
        device = Backend::CreateDevice();
        if (!device)
            GTEST_SKIP() << "No " << Backend::name << " device";
    }

    std::unique_ptr<typename Backend::Wrapper> MakeWrapper() {
        return std::make_unique<typename Backend::Wrapper>(InferenceConfig{}, Backend::Context(device.get()), inference,
                                                           [this](std::vector<ImageInference::IFrameBase::Ptr> frames) {
                                                               std::lock_guard lk(failed_mutex);
                                                               for (auto &frame : frames) {
                                                                   // As InferenceImpl does: the image goes, which
                                                                   // returns its texture to the pool
                                                                   frame->SetImage(nullptr);
                                                                   failed.push_back(frame);
                                                               }
                                                           });
    }

    // Submits a frame whose pattern fills `rect` of a `width` x `height` source; the ROI is `rect`
    // when `use_roi`, the whole frame otherwise.
    std::shared_ptr<TestFrame> Submit(typename Backend::Wrapper &wrapper, uint32_t width, uint32_t height, Roi rect,
                                      bool use_roi, typename Backend::Device *source_device = nullptr,
                                      typename Backend::Device *queried_device = nullptr) {
        auto *src_dev = source_device ? source_device : device.get();
        auto buffer =
            Backend::MakeSource(src_dev, width, height, QuadrantPattern(rect.x, rect.y, rect.width, rect.height));
        auto frame = std::make_shared<TestFrame>();
        auto image = Backend::SourceImage(buffer.get(), queried_device ? queried_device : src_dev);
        image->width = width;
        image->height = height;
        image->format = FourCC::FOURCC_BGRA;
        if (use_roi)
            image->rect = rect;
        frame->SetImage(image);
        sources.push_back(std::move(buffer));
        wrapper.SubmitImage(frame, {});
        return frame;
    }

    void SubmitFullFrame(typename Backend::Wrapper &wrapper, uint32_t width, uint32_t height,
                         typename Backend::Device *source_device = nullptr,
                         typename Backend::Device *queried_device = nullptr) {
        Submit(wrapper, width, height, {0, 0, width, height}, false, source_device, queried_device);
    }

    void ExpectAllQuadrants() {
        std::lock_guard lk(inference->mutex);
        for (size_t i = 0; i < inference->results.size(); i++) {
            SCOPED_TRACE("result " + std::to_string(i));
            ExpectQuadrants(inference->results[i], 0, 0, MODEL_WIDTH, MODEL_HEIGHT, inset);
        }
    }
};

using Backends = testing::Types<D3D11Backend, D3D12Backend>;

class BackendNames {
  public:
    template <typename T>
    static std::string GetName(int) {
        return T::name;
    }
};

} // namespace

TYPED_TEST_SUITE(D3DAsyncInferenceTest, Backends, BackendNames);

TYPED_TEST(D3DAsyncInferenceTest, SystemMemoryImageHoldsConvertedFrame) {
    auto wrapper = this->MakeWrapper();
    WithTimeout([&] {
        this->SubmitFullFrame(*wrapper, 160, 96);
        wrapper->Flush();
    });

    ASSERT_EQ(this->inference->ResultCount(), 1u);
    EXPECT_EQ(this->inference->result_types[0], MemoryType::SYSTEM);
    EXPECT_EQ(this->inference->result_formats[0], FourCC::FOURCC_BGRA);
    this->ExpectAllQuadrants();
}

TYPED_TEST(D3DAsyncInferenceTest, SurfaceSharingImageHoldsConvertedFrame) {
    this->inference->memory_type = TypeParam::surface_memory;
    auto *device = this->device.get();
    this->inference->read_target = [device](const Image &image) {
        EXPECT_TRUE(image.create_cl_buffer) << "the inference needs the callback to wrap the texture";
        return TypeParam::ReadSurfaceImage(device, image);
    };
    auto wrapper = this->MakeWrapper();
    WithTimeout([&] {
        this->SubmitFullFrame(*wrapper, 160, 96);
        wrapper->Flush();
    });

    ASSERT_EQ(this->inference->ResultCount(), 1u);
    EXPECT_EQ(this->inference->result_types[0], TypeParam::surface_memory);
    this->ExpectAllQuadrants();
}

TYPED_TEST(D3DAsyncInferenceTest, RoiOfImageIsConverted) {
    // inference-region=roi-list: only the detected object goes to the model
    auto wrapper = this->MakeWrapper();
    WithTimeout([&] {
        this->Submit(*wrapper, 256, 192, {40, 24, 120, 96}, true);
        wrapper->Flush();
    });

    ASSERT_EQ(this->inference->ResultCount(), 1u);
    this->ExpectAllQuadrants();
}

TYPED_TEST(D3DAsyncInferenceTest, StreamsOfDifferentSizesShareInstance) {
    // Streams sharing a model-instance-id alternate frame sizes; more sizes than the converter
    // cache holds force it to be emptied while the wrapper keeps running
    const std::vector<std::pair<uint32_t, uint32_t>> sizes = {{64, 48},   {128, 96},  {96, 64},   {160, 90},
                                                              {200, 120}, {80, 80},   {144, 108}, {176, 100},
                                                              {224, 126}, {120, 160}, {64, 48},   {128, 96}};
    auto wrapper = this->MakeWrapper();
    WithTimeout([&] {
        for (int round = 0; round < 2; round++)
            for (auto [w, h] : sizes)
                this->SubmitFullFrame(*wrapper, w, h);
        wrapper->Flush();
    });

    ASSERT_EQ(this->inference->ResultCount(), 2 * sizes.size());
    this->ExpectAllQuadrants();
}

TYPED_TEST(D3DAsyncInferenceTest, FailedSubmitReportsFrame) {
    // A frame the inference refuses must come back through the error handler, or the element
    // waits for it forever at EOS. Its texture must return to the pool, or the next frames block.
    this->inference->on_submit = [](ImageInference::IFrameBase::Ptr) { throw std::runtime_error("injected failure"); };
    auto wrapper = this->MakeWrapper();
    std::vector<ImageInference::IFrameBase::Ptr> submitted;
    const size_t count = 4 * this->inference->nireq; // well past the texture pool size
    WithTimeout([&] {
        for (size_t i = 0; i < count; i++)
            submitted.push_back(this->Submit(*wrapper, 128, 96, {0, 0, 128, 96}, false));
        wrapper->Flush();
    });

    std::lock_guard lk(this->failed_mutex);
    ASSERT_EQ(this->failed.size(), count);
    for (size_t i = 0; i < count; i++)
        EXPECT_EQ(this->failed[i], submitted[i]) << "frame " << i;
}

TYPED_TEST(D3DAsyncInferenceTest, FlushWaitsForFrameBeingSubmitted) {
    // EOS must not overtake a frame the worker is still handing to the inference
    auto inference = this->inference;
    inference->on_submit = [inference](ImageInference::IFrameBase::Ptr frame) {
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        inference->Record(*frame->GetImage());
        frame->SetImage(nullptr);
    };
    auto wrapper = this->MakeWrapper();
    WithTimeout([&] {
        for (int i = 0; i < 5; i++)
            this->SubmitFullFrame(*wrapper, 128, 96);
        wrapper->Flush();
        EXPECT_EQ(inference->ResultCount(), 5u);
    });
}

TYPED_TEST(D3DAsyncInferenceTest, DestroyWithFramesInInference) {
    // The inference still holds the images when the element is torn down. The wrapper must let it
    // finish them while the texture pool still exists.
    auto inference = this->inference;
    inference->on_submit = [inference](ImageInference::IFrameBase::Ptr frame) {
        std::lock_guard lk(inference->mutex);
        inference->held.push_back(std::move(frame));
    };
    WithTimeout([&] {
        auto wrapper = this->MakeWrapper();
        // No more than the pool holds, since nothing is released before the destructor
        for (size_t i = 0; i < inference->nireq; i++)
            this->SubmitFullFrame(*wrapper, 128, 96);
        // Wait for the worker to pass them on, as there is no Flush() here
        while (true) {
            std::lock_guard lk(inference->mutex);
            if (inference->held.size() == inference->nireq)
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        wrapper.reset();
    });
    EXPECT_TRUE(inference->held.empty());
}

// -- D3D11 only -----------------------------------------------------------------

class D3D11AsyncInferenceTest : public D3DAsyncInferenceTest<D3D11Backend> {
  protected:
    D3D11DevicePtr other_device;

    void SetUp() override {
        D3DAsyncInferenceTest<D3D11Backend>::SetUp();
        if (IsSkipped())
            return;
        // A second device on the same adapter, as a decoder that doesn't share its device has
        other_device = CreateD3D11Device();
        if (!other_device)
            GTEST_SKIP() << "No second D3D11 device";
    }
};

TEST_F(D3D11AsyncInferenceTest, FramesFromAnotherDeviceAreConverted) {
    auto wrapper = MakeWrapper();
    WithTimeout([&] {
        for (int i = 0; i < 3; i++)
            SubmitFullFrame(*wrapper, 160, 96, other_device.get());
        wrapper->Flush();
    });

    ASSERT_EQ(inference->ResultCount(), 3u);
    ExpectAllQuadrants();
}

TEST_F(D3D11AsyncInferenceTest, FramesFromAnotherDeviceAreSharedWithInferenceDevice) {
    // Surface sharing: converted on the frames' device, read on the inference device
    inference->memory_type = MemoryType::D3D11;
    auto *dev = device.get();
    inference->read_target = [dev](const Image &image) { return D3D11Backend::ReadSurfaceImage(dev, image); };
    auto wrapper = MakeWrapper();
    WithTimeout([&] {
        for (int i = 0; i < 3; i++)
            SubmitFullFrame(*wrapper, 160, 96, other_device.get());
        wrapper->Flush();
    });

    ASSERT_EQ(inference->ResultCount(), 3u);
    ExpectAllQuadrants();
}

TEST_F(D3D11AsyncInferenceTest, TextureDeviceWinsOverQueriedDevice) {
    // The upstream device query can name another device than the one the frames come from (for
    // example with streams from several devices merged). The texture's own device must be used:
    // converting on the queried one would read the texture through a foreign device.
    auto wrapper = MakeWrapper();
    WithTimeout([&] {
        for (int i = 0; i < 3; i++)
            SubmitFullFrame(*wrapper, 160, 96, device.get(), other_device.get());
        wrapper->Flush();
    });

    ASSERT_EQ(inference->ResultCount(), 3u);
    ExpectAllQuadrants();
}
