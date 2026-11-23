/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

// Tests for ImageInferenceAsyncD3D11/D3D12, the wrappers that pre-process frames on the GPU
// and hand them to the inference on a worker thread: the images they produce, the converter
// cache, frames from another device, and the error and teardown paths. Also for
// ImageInferenceAsyncOpenCL, which copies D3D11 and D3D12 frames into textures that OpenCL can
// read for pre-process-backend=opencl. The inference is a fake that inspects the images it is given.

#include "image_inference_async_d3d11.h"
#include "image_inference_async_d3d12.h"
#include "image_inference_async_opencl.h"

#include "dlstreamer/d3d11/context.h"
#include "dlstreamer/d3d12/context.h"
#include "inference_backend/image.h"
#include "inference_backend/logger.h"

#include "utils.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <future>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

// The inference backend's log function by default (logger.cpp)
void default_log_function(int level, const char *file, const char *function, int line, const char *format,
                          va_list args);

using namespace InferenceBackend;
using namespace d3d_test;

namespace {

using Roi = InferenceBackend::Rectangle<uint32_t>;

std::mutex log_mutex;
std::vector<std::string> *log_sink = nullptr; // guarded by log_mutex

void CapturingLogFunction(int level, const char *file, const char *function, int line, const char *format,
                          va_list args) {
    char message[1024];
    va_list copy;
    va_copy(copy, args);
    std::vsnprintf(message, sizeof(message), format, copy);
    va_end(copy);
    {
        std::lock_guard lk(log_mutex);
        if (log_sink)
            log_sink->push_back(message);
    }
    default_log_function(level, file, function, line, format, args);
}

// The messages the inference backend logs, from any thread, while alive
class LogCapture {
  public:
    LogCapture() {
        std::lock_guard lk(log_mutex);
        log_sink = &messages_;
        set_log_function(CapturingLogFunction);
    }
    ~LogCapture() {
        set_log_function(default_log_function);
        std::lock_guard lk(log_mutex);
        log_sink = nullptr;
    }
    LogCapture(const LogCapture &) = delete;
    LogCapture &operator=(const LogCapture &) = delete;

    size_t Count(const std::string &text) {
        std::lock_guard lk(log_mutex);
        return std::count_if(messages_.begin(), messages_.end(),
                             [&](const std::string &message) { return message.find(text) != std::string::npos; });
    }

  private:
    std::vector<std::string> messages_;
};

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
    std::vector<const void *> result_textures; // of D3D11 images

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
        result_textures.push_back(image.type == MemoryType::D3D11 ? image.d3d11_texture : nullptr);
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
    std::shared_ptr<TestFrame> Submit(ImageInference &wrapper, uint32_t width, uint32_t height, Roi rect, bool use_roi,
                                      typename Backend::Device *source_device = nullptr,
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

    void SubmitFullFrame(ImageInference &wrapper, uint32_t width, uint32_t height,
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

// -- ImageInferenceAsyncOpenCL (pre-process-backend=opencl) ---------------------

// The OpenCL pre-processing wrapper hands the inference D3D11 images that OpenCL can read: a shared copy of a D3D11
// frame, or the D3D11 view of a shareable copy of a D3D12 frame. The fake inference reads them back.
template <typename Backend>
class OpenclAsyncInferenceTest : public D3DAsyncInferenceTest<Backend> {
  protected:
    void SetUp() override {
        D3DAsyncInferenceTest<Backend>::SetUp();
        if (this->IsSkipped())
            return;
        this->inference->read_target = [](const Image &image) {
            EXPECT_EQ(image.type, MemoryType::D3D11);
            return D3D11Backend::ReadSurfaceImage(static_cast<GstD3D11Device *>(image.gst_d3d11_device), image);
        };
    }

    std::unique_ptr<ImageInferenceAsyncOpenCL> MakeOpenclWrapper() {
        auto d3d11_context = ImageInferenceAsyncOpenCL::D3D11ContextFor(Backend::Context(this->device.get()));
        return std::make_unique<ImageInferenceAsyncOpenCL>(InferenceConfig{}, d3d11_context, this->inference,
                                                           [this](std::vector<ImageInference::IFrameBase::Ptr> frames) {
                                                               std::lock_guard lk(this->failed_mutex);
                                                               for (auto &frame : frames) {
                                                                   frame->SetImage(nullptr);
                                                                   this->failed.push_back(frame);
                                                               }
                                                           });
    }
};

TYPED_TEST_SUITE(OpenclAsyncInferenceTest, Backends, BackendNames);

TYPED_TEST(OpenclAsyncInferenceTest, D3D11DeviceIsOnTheFramesAdapter) {
    auto d3d11_context = ImageInferenceAsyncOpenCL::D3D11ContextFor(TypeParam::Context(this->device.get()));
    auto *d3d11 = static_cast<GstD3D11Device *>(d3d11_context->handle(dlstreamer::D3D11Context::key::d3d11_device));
    ASSERT_NE(d3d11, nullptr);
    gint64 frames_luid = 0, d3d11_luid = 0;
    g_object_get(this->device.get(), "adapter-luid", &frames_luid, nullptr);
    g_object_get(d3d11, "adapter-luid", &d3d11_luid, nullptr);
    EXPECT_EQ(d3d11_luid, frames_luid);
}

TYPED_TEST(OpenclAsyncInferenceTest, ImageIsD3D11CopyOfFrame) {
    auto wrapper = this->MakeOpenclWrapper();
    WithTimeout([&] {
        this->SubmitFullFrame(*wrapper, 160, 96);
        wrapper->Flush();
    });

    ASSERT_EQ(this->inference->ResultCount(), 1u);
    EXPECT_EQ(this->inference->result_types[0], MemoryType::D3D11);
    const BgraImage &copy = this->inference->results[0];
    ASSERT_EQ(copy.width, 160u);
    ASSERT_EQ(copy.height, 96u);
    ExpectQuadrants(copy, 0, 0, 160, 96, 0); // copied, not resampled: exact up to the edges
}

TYPED_TEST(OpenclAsyncInferenceTest, RoisOfFrameShareOneCopy) {
    // The images of the ROIs of a frame, alive together as in a batch, read one copy of the frame
    auto inference = this->inference;
    inference->on_submit = [inference](ImageInference::IFrameBase::Ptr frame) {
        inference->Record(*frame->GetImage());
        std::lock_guard lk(inference->mutex);
        inference->held.push_back(std::move(frame));
    };
    auto wrapper = this->MakeOpenclWrapper();
    auto buffer = TypeParam::MakeSource(this->device.get(), 160, 96, QuadrantPattern(0, 0, 160, 96));
    WithTimeout([&] {
        for (Roi rect : {Roi{0, 0, 80, 48}, Roi{80, 48, 80, 48}}) {
            auto frame = std::make_shared<TestFrame>();
            auto image = TypeParam::SourceImage(buffer.get(), this->device.get());
            image->width = 160;
            image->height = 96;
            image->format = FourCC::FOURCC_BGRA;
            image->rect = rect;
            frame->SetImage(image);
            wrapper->SubmitImage(frame, {});
        }
        wrapper->Flush();
    });

    ASSERT_EQ(inference->ResultCount(), 2u);
    EXPECT_EQ(inference->result_textures[0], inference->result_textures[1]);
    ExpectQuadrants(inference->results[1], 0, 0, 160, 96, 0);
}

// The message logged when a pool of copies is created
template <typename Backend>
const char *PoolCreatedMessage() {
    return std::is_same_v<Backend, D3D11Backend> ? "OpenCL pre-processing: staging textures"
                                                 : "OpenCL pre-processing: shareable copies of";
}

TYPED_TEST(OpenclAsyncInferenceTest, StreamsOfDifferentSizesShareInstance) {
    // A pool per size: alternating sizes don't make a pool start over
    const std::vector<std::pair<uint32_t, uint32_t>> sizes = {{160, 96}, {96, 64}, {64, 48}};
    LogCapture log;
    auto wrapper = this->MakeOpenclWrapper();
    WithTimeout([&] {
        for (int round = 0; round < 4; round++)
            for (auto [w, h] : sizes)
                this->SubmitFullFrame(*wrapper, w, h);
        wrapper->Flush();
    });

    EXPECT_EQ(log.Count(PoolCreatedMessage<TypeParam>()), sizes.size());
    ASSERT_EQ(this->inference->ResultCount(), 4 * sizes.size());
    for (size_t i = 0; i < this->inference->results.size(); i++) {
        SCOPED_TRACE("result " + std::to_string(i));
        const auto [w, h] = sizes[i % sizes.size()];
        const BgraImage &copy = this->inference->results[i];
        ASSERT_EQ(copy.width, w);
        ASSERT_EQ(copy.height, h);
        ExpectQuadrants(copy, 0, 0, w, h, 0);
    }
}

TYPED_TEST(OpenclAsyncInferenceTest, IdlePoolIsDropped) {
    // A stream that stopped, or changed its frame size: its pool goes once kPoolIdleFrames frames of other sizes have
    // come, and a frame of its size gets a new one
    LogCapture log;
    auto inference = this->inference;
    auto released = std::make_shared<std::atomic<size_t>>(0);
    inference->on_submit = [released](ImageInference::IFrameBase::Ptr frame) {
        frame->SetImage(nullptr);
        ++*released;
    };
    auto wrapper = this->MakeOpenclWrapper();
    const uint64_t others = ImageInferenceAsyncOpenCL::kPoolIdleFrames + ImageInferenceAsyncOpenCL::kPoolCheckPeriod;
    WithTimeout([&] {
        this->SubmitFullFrame(*wrapper, 96, 64);
        for (uint64_t i = 0; i < others; i++)
            this->SubmitFullFrame(*wrapper, 64, 48);
        wrapper->Flush();
    });
    EXPECT_EQ(*released, others + 1);
    EXPECT_EQ(log.Count("dropping the idle pool"), 1u);
    EXPECT_EQ(log.Count("96x64"), 2u) << "created, then dropped";

    inference->on_submit = nullptr; // read the images back again
    WithTimeout([&] {
        this->SubmitFullFrame(*wrapper, 96, 64);
        wrapper->Flush();
    });
    EXPECT_EQ(log.Count(PoolCreatedMessage<TypeParam>()), 3u);
    ASSERT_EQ(inference->ResultCount(), 1u);
    ExpectQuadrants(inference->results[0], 0, 0, 96, 64, 0);
}

TYPED_TEST(OpenclAsyncInferenceTest, FrameSmallerThanItsTexture) {
    // Decoders align their textures (640x360 frames in 640x368 textures). The D3D12 copy covers the frame only.
    auto wrapper = this->MakeOpenclWrapper();
    auto buffer = TypeParam::MakeSource(this->device.get(), 160, 96, QuadrantPattern(0, 0, 150, 90));
    WithTimeout([&] {
        auto frame = std::make_shared<TestFrame>();
        auto image = TypeParam::SourceImage(buffer.get(), this->device.get());
        image->width = 150;
        image->height = 90;
        image->format = FourCC::FOURCC_BGRA;
        frame->SetImage(image);
        wrapper->SubmitImage(frame, {});
        wrapper->Flush();
    });

    ASSERT_EQ(this->inference->ResultCount(), 1u);
    ExpectQuadrants(this->inference->results[0], 0, 0, 150, 90, 0);
}

TYPED_TEST(OpenclAsyncInferenceTest, DestroyWithFramesInInference) {
    // The inference still holds the images, and with them the copies, when the element is torn down
    auto inference = this->inference;
    inference->on_submit = [inference](ImageInference::IFrameBase::Ptr frame) {
        std::lock_guard lk(inference->mutex);
        inference->held.push_back(std::move(frame));
    };
    WithTimeout([&] {
        auto wrapper = this->MakeOpenclWrapper();
        for (int i = 0; i < 3; i++)
            this->SubmitFullFrame(*wrapper, 160, 96);
        wrapper.reset();
    });
    EXPECT_TRUE(inference->held.empty());
}

TYPED_TEST(OpenclAsyncInferenceTest, FrameWithoutItsMemoryIsRejected) {
    auto wrapper = this->MakeOpenclWrapper();
    auto frame = std::make_shared<TestFrame>();
    auto image = std::make_shared<Image>();
    image->type = TypeParam::surface_memory;
    image->width = 64;
    image->height = 48;
    image->format = FourCC::FOURCC_BGRA;
    frame->SetImage(image);
    EXPECT_ANY_THROW(wrapper->SubmitImage(frame, {}));
    wrapper->Flush();
    EXPECT_EQ(this->inference->ResultCount(), 0u);
}

class OpenclD3D11AsyncInferenceTest : public OpenclAsyncInferenceTest<D3D11Backend> {};

TEST_F(OpenclD3D11AsyncInferenceTest, SharedFrameIsReadInPlace) {
    // A decoder's texture that is shared already is read as it is, without a copy
    auto buffer = AllocD3D11Texture(device.get(), 160, 96, DXGI_FORMAT_B8G8R8A8_UNORM,
                                    D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET, D3D11_RESOURCE_MISC_SHARED);
    FillD3D11(buffer.get(), QuadrantPattern(0, 0, 160, 96));
    auto wrapper = MakeOpenclWrapper();
    WithTimeout([&] {
        auto frame = std::make_shared<TestFrame>();
        auto image = D3D11Backend::SourceImage(buffer.get(), device.get());
        image->width = 160;
        image->height = 96;
        image->format = FourCC::FOURCC_BGRA;
        frame->SetImage(image);
        wrapper->SubmitImage(frame, {});
        wrapper->Flush();
    });

    ASSERT_EQ(inference->ResultCount(), 1u);
    EXPECT_EQ(inference->result_textures[0], D3D11Texture(buffer.get()));
    ExpectQuadrants(inference->results[0], 0, 0, 160, 96, 0);
}

TEST_F(OpenclD3D11AsyncInferenceTest, D3D12FramesOnTheAdapter) {
    // A model instance created for D3D11 frames, shared with a stream of D3D12 frames on its adapter, reads them
    // through its own D3D11 device
    D3D12DevicePtr d3d12_device = CreateD3D12Device();
    if (!d3d12_device)
        GTEST_SKIP() << "No D3D12 device";
    gint64 d3d11_luid = 0, d3d12_luid = 0;
    g_object_get(device.get(), "adapter-luid", &d3d11_luid, nullptr);
    g_object_get(d3d12_device.get(), "adapter-luid", &d3d12_luid, nullptr);
    if (d3d11_luid != d3d12_luid)
        GTEST_SKIP() << "The first D3D11 and D3D12 devices are on different adapters";

    auto wrapper = MakeOpenclWrapper();
    WithTimeout([&] {
        for (int i = 0; i < 4; i++) {
            if (i % 2 == 0) {
                SubmitFullFrame(*wrapper, 160, 96);
                continue;
            }
            auto buffer = D3D12Backend::MakeSource(d3d12_device.get(), 96, 64, QuadrantPattern(0, 0, 96, 64));
            auto frame = std::make_shared<TestFrame>();
            auto image = D3D12Backend::SourceImage(buffer.get(), d3d12_device.get());
            image->width = 96;
            image->height = 64;
            image->format = FourCC::FOURCC_BGRA;
            frame->SetImage(image);
            sources.push_back(std::move(buffer));
            wrapper->SubmitImage(frame, {});
        }
        wrapper->Flush();
    });

    ASSERT_EQ(inference->ResultCount(), 4u);
    for (size_t i = 0; i < inference->results.size(); i++) {
        SCOPED_TRACE("result " + std::to_string(i));
        const uint32_t width = i % 2 ? 96 : 160, height = i % 2 ? 64 : 96;
        ASSERT_EQ(inference->results[i].width, width);
        ExpectQuadrants(inference->results[i], 0, 0, width, height, 0);
    }
}

// Frames of a second D3D11 device on the adapter of the inference device, as from a decoder that doesn't share its
// device: copied on their device into textures shared with the inference device
class OpenclD3D11OtherDeviceTest : public OpenclD3D11AsyncInferenceTest {
  protected:
    D3D11DevicePtr other_device;

    void SetUp() override {
        OpenclD3D11AsyncInferenceTest::SetUp();
        if (IsSkipped())
            return;
        other_device = CreateD3D11Device();
        if (!other_device)
            GTEST_SKIP() << "No second D3D11 device";
    }
};

TEST_F(OpenclD3D11OtherDeviceTest, FramesAreCopiedOnTheirDevice) {
    // Frames of the inference device and of the other one alternate, at two sizes
    const std::vector<std::pair<uint32_t, uint32_t>> sizes = {{160, 96}, {96, 64}};
    LogCapture log;
    auto wrapper = MakeOpenclWrapper();
    WithTimeout([&] {
        for (int round = 0; round < 3; round++)
            for (auto [w, h] : sizes) {
                SubmitFullFrame(*wrapper, w, h);
                SubmitFullFrame(*wrapper, w, h, other_device.get());
            }
        wrapper->Flush();
    });

    ASSERT_EQ(inference->ResultCount(), 12u);
    for (size_t i = 0; i < inference->results.size(); i++) {
        SCOPED_TRACE("result " + std::to_string(i));
        const auto [w, h] = sizes[(i / 2) % sizes.size()];
        EXPECT_EQ(inference->result_types[i], MemoryType::D3D11);
        ASSERT_EQ(inference->results[i].width, w);
        ASSERT_EQ(inference->results[i].height, h);
        ExpectQuadrants(inference->results[i], 0, 0, w, h, 0);
    }
    EXPECT_EQ(log.Count("are copied there"), 1u);
    EXPECT_EQ(log.Count("textures shared from D3D11 device"), sizes.size()) << "a pool per size on the other device";
    EXPECT_EQ(log.Count("OpenCL pre-processing: staging textures"), sizes.size()) << "and on the inference device";
    std::lock_guard lk(failed_mutex);
    EXPECT_TRUE(failed.empty());
}

TEST_F(OpenclD3D11OtherDeviceTest, PooledTexturesAreOpenedOnce) {
    // The inference device opens each pooled texture once: an open makes a new texture, which OpenCL would import anew
    auto wrapper = MakeOpenclWrapper();
    WithTimeout([&] {
        for (int i = 0; i < 12; i++) {
            SubmitFullFrame(*wrapper, 96, 64, other_device.get());
            wrapper->Flush(); // each copy goes back to the pool before the next frame
        }
    });

    ASSERT_EQ(inference->ResultCount(), 12u);
    const std::set<const void *> textures(inference->result_textures.begin(), inference->result_textures.end());
    EXPECT_EQ(textures.size(), 1u);
    EXPECT_EQ(textures.count(nullptr), 0u);
    ExpectQuadrants(inference->results.back(), 0, 0, 96, 64, 0);
}

TEST_F(OpenclD3D11OtherDeviceTest, RoisOfFrameShareOneCopy) {
    auto inference = this->inference;
    inference->on_submit = [inference](ImageInference::IFrameBase::Ptr frame) {
        inference->Record(*frame->GetImage());
        std::lock_guard lk(inference->mutex);
        inference->held.push_back(std::move(frame));
    };
    auto wrapper = MakeOpenclWrapper();
    auto buffer = D3D11Backend::MakeSource(other_device.get(), 160, 96, QuadrantPattern(0, 0, 160, 96));
    WithTimeout([&] {
        for (Roi rect : {Roi{0, 0, 80, 48}, Roi{80, 48, 80, 48}}) {
            auto frame = std::make_shared<TestFrame>();
            auto image = D3D11Backend::SourceImage(buffer.get(), other_device.get());
            image->width = 160;
            image->height = 96;
            image->format = FourCC::FOURCC_BGRA;
            image->rect = rect;
            frame->SetImage(image);
            wrapper->SubmitImage(frame, {});
        }
        wrapper->Flush();
    });

    ASSERT_EQ(inference->ResultCount(), 2u);
    EXPECT_EQ(inference->result_textures[0], inference->result_textures[1]);
    EXPECT_NE(inference->result_textures[0], D3D11Texture(buffer.get()));
    ExpectQuadrants(inference->results[1], 0, 0, 160, 96, 0);
}

TEST_F(OpenclD3D11OtherDeviceTest, DestroyWithFramesInInference) {
    // The inference holds the copies, and through them the other device's pool, when the element is torn down
    auto inference = this->inference;
    inference->on_submit = [inference](ImageInference::IFrameBase::Ptr frame) {
        std::lock_guard lk(inference->mutex);
        inference->held.push_back(std::move(frame));
    };
    WithTimeout([&] {
        auto wrapper = MakeOpenclWrapper();
        for (int i = 0; i < 3; i++)
            SubmitFullFrame(*wrapper, 160, 96, other_device.get());
        wrapper.reset();
    });
    EXPECT_TRUE(inference->held.empty());
}

TEST_F(OpenclD3D11OtherDeviceTest, IdleDeviceIsDropped) {
    // A decoder that went away: its device is released once kPoolIdleFrames frames of others have come, and a frame
    // of it again starts over
    LogCapture log;
    auto inference = this->inference;
    inference->on_submit = [](ImageInference::IFrameBase::Ptr frame) { frame->SetImage(nullptr); };
    auto wrapper = MakeOpenclWrapper();
    WithTimeout([&] {
        SubmitFullFrame(*wrapper, 64, 48, other_device.get());
        for (uint64_t i = 0;
             i < ImageInferenceAsyncOpenCL::kPoolIdleFrames + ImageInferenceAsyncOpenCL::kPoolCheckPeriod; i++)
            SubmitFullFrame(*wrapper, 64, 48);
        wrapper->Flush();
    });
    EXPECT_EQ(log.Count("dropping the copies on idle D3D11 device"), 1u);

    inference->on_submit = nullptr; // read the images back again
    WithTimeout([&] {
        SubmitFullFrame(*wrapper, 64, 48, other_device.get());
        wrapper->Flush();
    });
    EXPECT_EQ(log.Count("are copied there"), 2u);
    ASSERT_EQ(inference->ResultCount(), 1u);
    ExpectQuadrants(inference->results[0], 0, 0, 64, 48, 0);
}

class OpenclD3D12AsyncInferenceTest : public OpenclAsyncInferenceTest<D3D12Backend> {};

TEST_F(OpenclD3D12AsyncInferenceTest, D3D11FramesOfAnotherDeviceAreCopied) {
    // A model instance created for D3D12 frames reads through a D3D11 device of its own, shared with a stream of D3D11
    // frames on the adapter: they are copied on their device
    D3D11DevicePtr d3d11_device = CreateD3D11Device();
    if (!d3d11_device)
        GTEST_SKIP() << "No D3D11 device";
    gint64 d3d11_luid = 0, d3d12_luid = 0;
    g_object_get(d3d11_device.get(), "adapter-luid", &d3d11_luid, nullptr);
    g_object_get(device.get(), "adapter-luid", &d3d12_luid, nullptr);
    if (d3d11_luid != d3d12_luid)
        GTEST_SKIP() << "The first D3D11 and D3D12 devices are on different adapters";

    auto wrapper = MakeOpenclWrapper();
    WithTimeout([&] {
        for (int i = 0; i < 4; i++) {
            if (i % 2 == 0) {
                SubmitFullFrame(*wrapper, 160, 96);
                continue;
            }
            auto buffer = D3D11Backend::MakeSource(d3d11_device.get(), 96, 64, QuadrantPattern(0, 0, 96, 64));
            auto frame = std::make_shared<TestFrame>();
            auto image = D3D11Backend::SourceImage(buffer.get(), d3d11_device.get());
            image->width = 96;
            image->height = 64;
            image->format = FourCC::FOURCC_BGRA;
            frame->SetImage(image);
            sources.push_back(std::move(buffer));
            wrapper->SubmitImage(frame, {});
        }
        wrapper->Flush();
    });

    ASSERT_EQ(inference->ResultCount(), 4u);
    for (size_t i = 0; i < inference->results.size(); i++) {
        SCOPED_TRACE("result " + std::to_string(i));
        const uint32_t width = i % 2 ? 96 : 160, height = i % 2 ? 64 : 96;
        ASSERT_EQ(inference->results[i].width, width);
        ExpectQuadrants(inference->results[i], 0, 0, width, height, 0);
    }
    std::lock_guard lk(failed_mutex);
    EXPECT_TRUE(failed.empty());
}

TEST_F(OpenclD3D12AsyncInferenceTest, FramesFromAnotherDeviceObjectOfTheAdapter) {
    // GStreamer hands out a GstD3D12Device object per user of an adapter's device, so the decoder's frames and the
    // inference context can come with different ones. They are the same device.
    D3D12DevicePtr other_device = CreateD3D12Device();
    ASSERT_TRUE(other_device);
    ASSERT_NE(other_device.get(), device.get());
    auto wrapper = MakeOpenclWrapper();
    WithTimeout([&] {
        for (int i = 0; i < 4; i++)
            SubmitFullFrame(*wrapper, 160, 96, i % 2 ? other_device.get() : device.get());
        wrapper->Flush();
    });

    ASSERT_EQ(inference->ResultCount(), 4u);
    for (size_t i = 0; i < inference->results.size(); i++) {
        SCOPED_TRACE("result " + std::to_string(i));
        ExpectQuadrants(inference->results[i], 0, 0, 160, 96, 0);
    }
    std::lock_guard lk(failed_mutex);
    EXPECT_TRUE(failed.empty());
}
