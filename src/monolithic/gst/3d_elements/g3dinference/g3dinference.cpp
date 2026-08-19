/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "g3dinference.h"

#include "gmutex_lock_guard.h"
#include <dlstreamer/gst/metadata/g3d_lidar_meta.h>
#include <dlstreamer/gst/metadata/g3d_od_mtd.h>
#include <gst/analytics/analytics.h>
#include <nlohmann/json.hpp>
#include <openvino/openvino.hpp>

#ifdef __linux__
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

GST_DEBUG_CATEGORY_STATIC(gst_g3d_inference_debug);
#define GST_CAT_DEFAULT gst_g3d_inference_debug

using json = nlohmann::json;

enum {
    PROP_0,
    PROP_CONFIG,
    PROP_DEVICE,
    PROP_MODEL_TYPE,
    PROP_SCORE_THRESHOLD,
    PROP_NIREQ,
};

namespace {

constexpr const char *DEFAULT_DEVICE = "CPU";
constexpr const char *SUPPORTED_DEVICE_CPU = "CPU";
constexpr const char *SUPPORTED_DEVICE_GPU = "GPU";
constexpr const char *DEFAULT_MODEL_TYPE = "pointpillars";
constexpr float DEFAULT_SCORE_THRESHOLD = 0.7f;
constexpr size_t POINT_SIZE = 4;
constexpr size_t DETECTION_WIDTH = 9;
constexpr guint DEFAULT_NIREQ = 0; /* 0 = derive from the compiled NN model */
constexpr guint MAX_NIREQ = 1024;

bool is_supported_device(const gchar *device) {
    if (!device || !*device)
        return false;

    if (g_ascii_strcasecmp(device, SUPPORTED_DEVICE_CPU) == 0)
        return true;

    if (g_ascii_strncasecmp(device, SUPPORTED_DEVICE_GPU, strlen(SUPPORTED_DEVICE_GPU)) != 0)
        return false;

    const gchar *suffix = device + strlen(SUPPORTED_DEVICE_GPU);
    if (*suffix == '\0')
        return true;

    if (*suffix != '.')
        return false;

    ++suffix;
    if (!g_ascii_isdigit(*suffix))
        return false;

    while (*suffix) {
        if (!g_ascii_isdigit(*suffix))
            return false;
        ++suffix;
    }

    return true;
}

/* A private voxel/nn/postproc InferRequest triplet. Each worker thread owns one
 * for the whole lifetime of a frame, so no ov::InferRequest is ever touched
 * concurrently from two threads. */
struct InferChain {
    ov::InferRequest voxel;
    ov::InferRequest nn;
    ov::InferRequest postproc;
};

/* Bounded work queue feeding the worker pool. pop() blocks while empty and
 * returns false once stop() has been called and the backlog is drained, which
 * is how workers are woken up for shutdown. push() blocks while the queue holds
 * @limit items, giving the streaming thread backpressure instead of letting an
 * unbounded backlog build up. */
class TaskQueue {
  public:
    /* Invoked on a worker thread with that worker's private InferChain. */
    using Task = std::function<void(InferChain &)>;

    void set_limit(size_t limit) {
        std::lock_guard<std::mutex> lock(_mutex);
        _limit = limit;
    }

    bool push(Task task) {
        {
            std::unique_lock<std::mutex> lock(_mutex);
            _not_full.wait(lock, [this] { return _stopped || _unblocked || _limit == 0 || _tasks.size() < _limit; });
            if (_stopped || _unblocked)
                return false;
            _tasks.push_back(std::move(task));
        }
        _not_empty.notify_one();
        return true;
    }

    bool pop(Task &task) {
        std::unique_lock<std::mutex> lock(_mutex);
        _not_empty.wait(lock, [this] { return _stopped || !_tasks.empty(); });
        if (_tasks.empty())
            return false;
        task = std::move(_tasks.front());
        _tasks.pop_front();
        lock.unlock();
        _not_full.notify_one();
        return true;
    }

    void stop() {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _stopped = true;
        }
        _not_empty.notify_all();
        _not_full.notify_all();
    }

    /* Release a submitter that is blocked because the queue is full, and reject
     * further submissions until set_unblocked(false). FLUSH_START arrives on a
     * different thread than the blocked streaming thread, so this is what lets
     * a flush through when downstream has stalled the workers. */
    void set_unblocked(bool unblocked) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _unblocked = unblocked;
        }
        _not_full.notify_all();
    }

  private:
    std::deque<Task> _tasks;
    std::mutex _mutex;
    std::condition_variable _not_empty;
    std::condition_variable _not_full;
    size_t _limit = 0;
    bool _stopped = false;
    bool _unblocked = false;
};

class PointPillarsRuntime {
  public:
    ~PointPillarsRuntime() {
        shutdown();
    }

    /* Number of frames that may be in flight concurrently. */
    size_t concurrency() const {
        return _chains.size();
    }

    /* Hand @task to the worker pool. The task receives the calling worker's
     * private InferChain. Blocks while every worker is busy and the queue is
     * full, which throttles the streaming thread. Returns false if the pool is
     * shutting down. */
    bool submit(TaskQueue::Task task) {
        return _queue.push(std::move(task));
    }

    /* See TaskQueue::set_unblocked(). Used to break a full queue during flush. */
    void set_submit_unblocked(bool unblocked) {
        _queue.set_unblocked(unblocked);
    }

    void shutdown() {
        /* stop() releases both submitters blocked on a full queue and workers
         * blocked waiting for work, so the joins below cannot hang. */
        _queue.stop();
        for (std::thread &worker : _workers) {
            if (worker.joinable())
                worker.join();
        }
        _workers.clear();
    }

    void load(const std::string &config_path, const std::string &device, guint nireq) {
        std::ifstream stream(config_path);
        if (!stream)
            throw std::runtime_error("Failed to open config: " + config_path);

        json config_json;
        stream >> config_json;

        if (config_json.contains("voxel_params")) {
            GST_WARNING("Config '%s' contains voxel_params, but g3dinference ignores voxel_params at runtime. "
                        "Voxelization settings must be baked into the exported PointPillars models.",
                        config_path.c_str());
        }

        const std::filesystem::path config_dir = std::filesystem::path(config_path).parent_path();
        auto resolve_path = [&config_dir](const std::string &path_str) {
            const std::filesystem::path path(path_str);
            if (path.is_absolute())
                return path.lexically_normal().string();
            return (config_dir / path).lexically_normal().string();
        };

        _extension_lib = resolve_path(config_json.at("extension_lib").get<std::string>());
        _voxel_model_path = resolve_path(config_json.at("voxel_model").get<std::string>());
        _nn_model_path = resolve_path(config_json.at("nn_model").get<std::string>());
        _postproc_model_path = resolve_path(config_json.at("postproc_model").get<std::string>());
        _device = device;

        _core.add_extension(_extension_lib);

        /* The voxelization and post-processing stages always run on CPU. The
         * CPU plugin otherwise pins each request's threads, so with several
         * requests in flight every worker ends up bound to the same core,
         * saturating it while the rest of the machine idles. Disabling thread
         * binding lets the scheduler spread the workers out.
         *
         * Every stage runs under the THROUGHPUT hint so that it opens several
         * execution streams and the frames this element keeps in flight really
         * do execute concurrently. The LATENCY hint would instead pin each stage
         * to a single stream, capping concurrency at one request per stage no
         * matter how large nireq is. */
        ov::AnyMap cpu_stage_config = {
            {ov::hint::enable_cpu_pinning.name(), false},
            {ov::hint::performance_mode.name(), ov::hint::PerformanceMode::THROUGHPUT},
        };

        /* The network stage takes the same hint, but never carries the CPU
         * pinning override: that key is CPU-plugin specific and is rejected by
         * the GPU plugin. */
        ov::AnyMap nn_config = {{ov::hint::performance_mode.name(), ov::hint::PerformanceMode::THROUGHPUT}};

        /* Tell the plugins how many requests will actually be submitted so they
         * size their stream pools to match this element's worker pool instead of
         * guessing from the device. A plugin that opens more streams than we can
         * feed just splits its threads across streams that stay idle; one told
         * the real request count folds those threads back into the streams that
         * do run. Only meaningful when nireq is explicit -- nireq=0 derives the
         * pool size from the plugin's own estimate, so there is nothing to
         * align. */
        if (nireq > 0) {
            cpu_stage_config[ov::hint::num_requests.name()] = static_cast<uint32_t>(nireq);
            nn_config[ov::hint::num_requests.name()] = static_cast<uint32_t>(nireq);
        }

        _compiled_voxel = _core.compile_model(_core.read_model(_voxel_model_path), "CPU", cpu_stage_config);
        _compiled_nn = _core.compile_model(_core.read_model(_nn_model_path), _device, nn_config);
        _compiled_postproc = _core.compile_model(_core.read_model(_postproc_model_path), "CPU", cpu_stage_config);

        const size_t pool_size = resolve_pool_size(nireq);
        _chains.reserve(pool_size);
        for (size_t i = 0; i < pool_size; ++i) {
            auto chain = std::make_unique<InferChain>();
            chain->voxel = _compiled_voxel.create_infer_request();
            chain->nn = _compiled_nn.create_infer_request();
            chain->postproc = _compiled_postproc.create_infer_request();
            _chains.push_back(std::move(chain));
        }

        /* Allow one queued frame per worker on top of the in-flight ones, so a
         * worker finishing a frame always has the next one ready to start. */
        _queue.set_limit(pool_size * 2);
        for (size_t i = 0; i < pool_size; ++i) {
            InferChain *chain = _chains[i].get();
            _workers.emplace_back([this, chain] { worker_loop(*chain); });
        }
    }

    std::vector<float> infer(InferChain &chain, const float *points, size_t point_count, float score_threshold) {
        if (point_count == 0)
            return {};

        ov::Tensor points_tensor(ov::element::f32, ov::Shape{point_count, POINT_SIZE}, const_cast<float *>(points));
        chain.voxel.set_input_tensor(0, points_tensor);
        chain.voxel.infer();

        chain.nn.set_input_tensor(0, chain.voxel.get_output_tensor(0));
        chain.nn.set_input_tensor(1, chain.voxel.get_output_tensor(1));
        chain.nn.set_input_tensor(2, chain.voxel.get_output_tensor(2));
        chain.nn.infer();

        chain.postproc.set_input_tensor(0, squeeze_leading_dim(chain.nn.get_output_tensor(0)));
        chain.postproc.set_input_tensor(1, squeeze_leading_dim(chain.nn.get_output_tensor(1)));
        chain.postproc.set_input_tensor(2, squeeze_leading_dim(chain.nn.get_output_tensor(2)));
        chain.postproc.infer();

        return collect_detections(chain.postproc.get_output_tensor(0), chain.postproc.get_output_tensor(1),
                                  chain.postproc.get_output_tensor(2), score_threshold);
    }

  private:
    /* Each worker owns @chain for its whole lifetime, so no ov::InferRequest is
     * ever used by two threads at once and no locking is needed around it. */
    void worker_loop(InferChain &chain) {
        reset_thread_affinity();

        TaskQueue::Task task;
        while (_queue.pop(task)) {
            task(chain);
            task = nullptr;
        }
    }

    /* Undo any CPU pinning inherited from the thread that spawned this worker.
     *
     * The OpenVINO CPU plugin pins the thread that compiles/creates a request,
     * and std::thread inherits the creator's affinity mask. Without this reset
     * every worker ends up bound to the same single core, so the pool serializes
     * on one CPU while the rest of the machine sits idle. */
    static void reset_thread_affinity() {
#ifdef __linux__
        const long cores = sysconf(_SC_NPROCESSORS_ONLN);
        if (cores <= 0)
            return;

        cpu_set_t *set = CPU_ALLOC(cores);
        if (!set)
            return;

        const size_t size = CPU_ALLOC_SIZE(cores);
        CPU_ZERO_S(size, set);
        for (long cpu = 0; cpu < cores; ++cpu)
            CPU_SET_S(cpu, size, set);

        if (pthread_setaffinity_np(pthread_self(), size, set) != 0)
            GST_WARNING("Failed to reset worker thread CPU affinity; inference may be confined to one core");

        CPU_FREE(set);
#endif
    }

    /* nireq=0 means "ask the compiled NN model". The NN stage is the one that
     * runs on the accelerator, so its optimal request count is what determines
     * how many frames should be kept in flight. */
    size_t resolve_pool_size(guint nireq) const {
        if (nireq > 0)
            return nireq;

        try {
            const auto optimal = _compiled_nn.get_property(ov::optimal_number_of_infer_requests);
            if (optimal > 0)
                return static_cast<size_t>(optimal);
        } catch (const std::exception &e) {
            GST_WARNING("Failed to query optimal_number_of_infer_requests (%s), falling back to 1 request", e.what());
        }
        return 1;
    }

    static ov::Tensor squeeze_leading_dim(const ov::Tensor &tensor) {
        ov::Shape shape = tensor.get_shape();
        if (!shape.empty() && shape.front() == 1) {
            ov::Shape squeezed(shape.begin() + 1, shape.end());
            return ov::Tensor(tensor.get_element_type(), squeezed, const_cast<void *>(tensor.data()));
        }
        return tensor;
    }

    static std::vector<float> tensor_to_float_vector(const ov::Tensor &tensor) {
        const size_t total = tensor.get_size();
        std::vector<float> values(total, 0.0f);

        switch (tensor.get_element_type()) {
        case ov::element::f32: {
            const float *data = tensor.data<const float>();
            std::copy(data, data + total, values.begin());
            break;
        }
        case ov::element::i32: {
            const int32_t *data = tensor.data<const int32_t>();
            std::transform(data, data + total, values.begin(), [](int32_t v) { return static_cast<float>(v); });
            break;
        }
        case ov::element::i64: {
            const int64_t *data = tensor.data<const int64_t>();
            std::transform(data, data + total, values.begin(), [](int64_t v) { return static_cast<float>(v); });
            break;
        }
        case ov::element::u32: {
            const uint32_t *data = tensor.data<const uint32_t>();
            std::transform(data, data + total, values.begin(), [](uint32_t v) { return static_cast<float>(v); });
            break;
        }
        case ov::element::u64: {
            const uint64_t *data = tensor.data<const uint64_t>();
            std::transform(data, data + total, values.begin(), [](uint64_t v) { return static_cast<float>(v); });
            break;
        }
        default:
            throw std::runtime_error("Unsupported output tensor element type");
        }

        return values;
    }

    static std::vector<float> collect_detections(const ov::Tensor &bboxes, const ov::Tensor &labels,
                                                 const ov::Tensor &scores, float score_threshold) {
        const ov::Shape bbox_shape = bboxes.get_shape();
        if (bbox_shape.size() != 2 || bbox_shape[1] != 7)
            throw std::runtime_error("Unexpected bbox tensor shape");

        const size_t bbox_count = bbox_shape[0];
        const float *bbox_data = bboxes.data<const float>();
        const std::vector<float> label_data = tensor_to_float_vector(labels);
        const std::vector<float> score_data = tensor_to_float_vector(scores);
        const size_t count = std::min({bbox_count, label_data.size(), score_data.size()});

        std::vector<float> flattened;
        flattened.reserve(count * DETECTION_WIDTH);
        for (size_t index = 0; index < count; ++index) {
            if (score_data[index] < score_threshold)
                continue;

            const float *bbox = bbox_data + index * 7;
            flattened.insert(flattened.end(), bbox, bbox + 7);
            flattened.push_back(score_data[index]);
            flattened.push_back(label_data[index]);
        }
        return flattened;
    }

    ov::Core _core;
    ov::CompiledModel _compiled_voxel;
    ov::CompiledModel _compiled_nn;
    ov::CompiledModel _compiled_postproc;

    /* One chain per worker thread, indexed by worker. */
    std::vector<std::unique_ptr<InferChain>> _chains;

    TaskQueue _queue;
    std::vector<std::thread> _workers;

    std::string _device;
    std::string _extension_lib;
    std::string _voxel_model_path;
    std::string _nn_model_path;
    std::string _postproc_model_path;
};

LidarMeta *get_lidar_meta(GstBuffer *buffer) {
    return reinterpret_cast<LidarMeta *>(gst_buffer_get_meta(buffer, LIDAR_META_API_TYPE));
}

PointPillarsRuntime *get_runtime(GstG3DInference *filter) {
    return reinterpret_cast<PointPillarsRuntime *>(filter->runtime);
}

GstClockTime get_exit_g3dinference_timestamp(GstG3DInference *filter) {
    if (GstClock *clock = gst_element_get_clock(GST_ELEMENT(filter))) {
        GstClockTime timestamp = gst_clock_get_time(clock);
        GST_DEBUG_OBJECT(filter, "exit_g3dinference_timestamp from element clock: %" GST_TIME_FORMAT,
                         GST_TIME_ARGS(timestamp));
        gst_object_unref(clock);
        return timestamp;
    }

    GstClockTime timestamp = gst_util_get_timestamp();
    GST_DEBUG_OBJECT(filter, "exit_g3dinference_timestamp from gst_util_get_timestamp: %" GST_TIME_FORMAT,
                     GST_TIME_ARGS(timestamp));
    return timestamp;
}

/* Emit one GstAnalytics3DODMtd per detection onto @rmeta. Each detection is
 * DETECTION_WIDTH floats in PointPillars layout: x, y, z, w, l, h, theta, score,
 * label. The GstAnalytics3DODMtd setter takes (length, width, height), so the
 * model's w maps to width (d[3]) and l maps to length (d[4]). PointPillars reports
 * z at the box's bottom-center (its lower face), but the mtd stores the box centre,
 * so raise z by half the height. Returns the number of 3D detections written. */
size_t emit_3d_od_mtds(GstAnalyticsRelationMeta *rmeta, const std::vector<float> &detections) {
    const size_t count = detections.size() / DETECTION_WIDTH;
    size_t written = 0;
    for (size_t i = 0; i < count; ++i) {
        const float *d = detections.data() + i * DETECTION_WIDTH;
        GstAnalytics3DODMtd mtd;
        if (gst_analytics_relation_meta_add_3d_od_mtd(rmeta, /*x=*/d[0], /*y=*/d[1], /*z=*/d[2] + d[5] * 0.5f,
                                                      /*length=*/d[4], /*width=*/d[3], /*height=*/d[5], /*yaw=*/d[6],
                                                      /*pitch=*/0.f, /*roll=*/0.f, /*class_id=*/static_cast<gint>(d[8]),
                                                      /*confidence=*/d[7], GST_ANALYTICS_3D_SENSOR_LIDAR, &mtd)) {
            ++written;
        } else {
            GST_WARNING("Failed to add GstAnalytics3DODMtd for detection %zu/%zu", i, count);
        }
    }
    return written;
}

/* One in-flight frame. Created on the streaming thread in submission order,
 * completed on a worker thread. The buffer stays mapped until the worker is
 * done reading the point cloud out of it. */
struct PendingFrame {
    GstBuffer *buffer = nullptr;
    GstMapInfo map_info = {};
    bool mapped = false;
    /* Written by the worker that finishes this frame, read by whichever thread
     * drains the queue, so it has to be atomic. */
    std::atomic<bool> ready{false};
    GstFlowReturn status = GST_FLOW_OK;
    std::string error;

    void unmap() {
        if (mapped) {
            gst_buffer_unmap(buffer, &map_info);
            mapped = false;
        }
    }
};

using PendingFramePtr = std::shared_ptr<PendingFrame>;

/* Keeps output in submission order while frames complete out of order.
 * Completed frames are only released once every frame ahead of them has also
 * completed, mirroring the queue in gvainference. */
class OutputQueue {
  public:
    void push(PendingFramePtr frame) {
        std::lock_guard<std::mutex> lock(_mutex);
        _frames.push_back(std::move(frame));
    }

    /* Detach the leading run of completed frames. Anything after the first
     * still-running frame stays queued to preserve ordering. */
    std::list<PendingFramePtr> take_ready() {
        std::list<PendingFramePtr> ready;
        std::lock_guard<std::mutex> lock(_mutex);
        auto it = std::find_if(_frames.begin(), _frames.end(),
                               [](const PendingFramePtr &f) { return !f->ready.load(std::memory_order_acquire); });
        ready.splice(ready.begin(), _frames, _frames.begin(), it);
        /* Frames taken here are still owned by the caller until it reports back
         * via finish(), so the queue is not considered drained yet. */
        _unfinished += ready.size();
        return ready;
    }

    /* Drop everything still queued, unmapping and unreffing as we go. Used on
     * flush, where in-flight results are discarded rather than pushed. */
    std::list<PendingFramePtr> take_all() {
        std::list<PendingFramePtr> all;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            all.swap(_frames);
        }
        _drained.notify_all();
        return all;
    }

    /* True if the frame at the head of the queue has completed, i.e. there is
     * something take_ready() would hand out. */
    bool has_ready() {
        std::lock_guard<std::mutex> lock(_mutex);
        return !_frames.empty() && _frames.front()->ready.load(std::memory_order_acquire);
    }

    /* Called once the caller of take_ready() has finished disposing of @count
     * frames (pushed or discarded). */
    void finish(size_t count) {
        if (count == 0)
            return;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _unfinished -= count;
        }
        _drained.notify_all();
    }

    /* Waits until nothing is queued and nothing is still being pushed. */
    void wait_until_drained() {
        std::unique_lock<std::mutex> lock(_mutex);
        _drained.wait(lock, [this] { return _frames.empty() && _unfinished == 0; });
    }

  private:
    std::list<PendingFramePtr> _frames;
    size_t _unfinished = 0;
    std::mutex _mutex;
    std::condition_variable _drained;
};

/* Element-owned async state. Allocated for the lifetime of the element so the
 * public C struct only needs an opaque pointer. */
struct G3DInferenceAsyncState {
    OutputQueue queue;
    std::mutex push_mutex; /* serializes gst_pad_push across worker threads */
    std::atomic<bool> flushing{false};
    std::atomic<GstFlowReturn> last_flow{GST_FLOW_OK};

    /* Number of frames submitted to the pool but not yet processed. */
    int in_flight = 0;
    std::mutex in_flight_mutex;
    std::condition_variable in_flight_idle;

    void begin_frame() {
        std::lock_guard<std::mutex> lock(in_flight_mutex);
        ++in_flight;
    }

    void end_frame() {
        {
            std::lock_guard<std::mutex> lock(in_flight_mutex);
            --in_flight;
        }
        in_flight_idle.notify_all();
    }

    void wait_idle() {
        std::unique_lock<std::mutex> lock(in_flight_mutex);
        in_flight_idle.wait(lock, [this] { return in_flight == 0; });
    }
};

G3DInferenceAsyncState *get_async_state(GstG3DInference *filter) {
    return reinterpret_cast<G3DInferenceAsyncState *>(filter->async_state);
}

/* Release a frame without pushing it: unmap, unref, drop. */
void discard_frame(const PendingFramePtr &frame) {
    frame->unmap();
    if (frame->buffer) {
        gst_buffer_unref(frame->buffer);
        frame->buffer = nullptr;
    }
}

/* Push every frame whose turn has come. Runs on whichever worker thread
 * completed the head of the queue; the push mutex keeps two workers from
 * interleaving pushes. */
void push_ready_frames(GstG3DInference *filter) {
    G3DInferenceAsyncState *state = get_async_state(filter);

    /* Only one thread pushes at a time, but a worker that loses the race must
     * not block behind the pusher -- otherwise every worker ends up serialized
     * on this mutex and the pool degenerates to a single active thread. The
     * winner re-checks the queue after releasing the lock (see the retry
     * below), so frames handed over by a losing worker are still pushed. */
    std::unique_lock<std::mutex> push_lock(state->push_mutex, std::try_to_lock);
    if (!push_lock.owns_lock())
        return;

    for (;;) {
        std::list<PendingFramePtr> ready = state->queue.take_ready();
        if (ready.empty()) {
            /* Drop the lock, then look once more: a worker may have marked a
             * frame ready between take_ready() and the unlock, and seen the
             * mutex held so skipped its own push. Re-acquiring closes that
             * window; if there is still nothing, we are genuinely done. */
            push_lock.unlock();
            if (!state->queue.has_ready())
                return;
            push_lock.lock();
            continue;
        }

        const size_t count = ready.size();
        for (const PendingFramePtr &frame : ready) {
            frame->unmap();

            if (frame->status != GST_FLOW_OK) {
                GST_ELEMENT_ERROR(filter, STREAM, FAILED, ("Failed to process LiDAR buffer"),
                                  ("%s", frame->error.c_str()));
                state->last_flow.store(frame->status);
                discard_frame(frame);
                continue;
            }

            if (state->flushing.load()) {
                discard_frame(frame);
                continue;
            }

            GstBuffer *buffer = frame->buffer;
            frame->buffer = nullptr;
            GstFlowReturn ret = gst_pad_push(GST_BASE_TRANSFORM_SRC_PAD(filter), buffer);
            if (ret != GST_FLOW_OK) {
                /* FLUSHING/EOS are normal shutdown outcomes, not errors. */
                if (ret != GST_FLOW_FLUSHING && ret != GST_FLOW_EOS)
                    GST_WARNING_OBJECT(filter, "gst_pad_push returned %s", gst_flow_get_name(ret));
                state->last_flow.store(ret);
            }
        }

        /* Only now are these frames fully disposed of, so a concurrent EOS
         * drain may stop waiting on them. */
        state->queue.finish(count);
    }
}

/* Block until every submitted frame has been processed AND pushed downstream.
 *
 * The queue tracks frames that have been handed to a pusher but not yet
 * disposed of, so this returns only once the tail of the stream has actually
 * left the element -- which is what keeps EOS from overtaking it. Workers do
 * the pushing, so this thread only waits. */
void drain_pending(GstG3DInference *filter) {
    G3DInferenceAsyncState *state = get_async_state(filter);
    if (!state)
        return;

    /* Wait for the workers to finish computing, then push what they produced
     * from this thread. Workers use try_lock and give up if another thread is
     * already pushing, so the last completions may have left frames queued with
     * nobody to push them -- this thread takes that job. */
    state->wait_idle();
    push_ready_frames(filter);
    state->queue.wait_until_drained();
}

/* Block until no worker is still executing a frame. Unlike drain_pending() this
 * does not require the frames to be pushed, so it is safe to call while
 * flushing (where results are dropped rather than pushed downstream). */
void wait_for_in_flight(GstG3DInference *filter) {
    if (G3DInferenceAsyncState *state = get_async_state(filter))
        state->wait_idle();
}

} // namespace

static GstStaticPadTemplate sink_template =
    GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS("application/x-lidar"));

static GstStaticPadTemplate src_template =
    GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS("application/x-lidar"));

static void gst_g3d_inference_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void gst_g3d_inference_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);
static void gst_g3d_inference_finalize(GObject *object);
static gboolean gst_g3d_inference_start(GstBaseTransform *trans);
static gboolean gst_g3d_inference_stop(GstBaseTransform *trans);
static GstFlowReturn gst_g3d_inference_transform_ip(GstBaseTransform *trans, GstBuffer *buffer);
static GstFlowReturn gst_g3d_inference_generate_output(GstBaseTransform *trans, GstBuffer **outbuf);
static gboolean gst_g3d_inference_sink_event(GstBaseTransform *trans, GstEvent *event);
static GstCaps *gst_g3d_inference_transform_caps(GstBaseTransform *trans, GstPadDirection direction, GstCaps *caps,
                                                 GstCaps *filter);

G_DEFINE_TYPE(GstG3DInference, gst_g3d_inference, GST_TYPE_BASE_TRANSFORM);

static void gst_g3d_inference_class_init(GstG3DInferenceClass *klass) {
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstElementClass *gstelement_class = GST_ELEMENT_CLASS(klass);
    GstBaseTransformClass *base_transform_class = GST_BASE_TRANSFORM_CLASS(klass);

    GST_DEBUG_CATEGORY_INIT(gst_g3d_inference_debug, "g3dinference", 0, "3D LiDAR inference element");

    gobject_class->set_property = gst_g3d_inference_set_property;
    gobject_class->get_property = gst_g3d_inference_get_property;
    gobject_class->finalize = gst_g3d_inference_finalize;

    g_object_class_install_property(gobject_class, PROP_CONFIG,
                                    g_param_spec_string("config", "Config",
                                                        "Path to PointPillars OpenVINO JSON configuration", NULL,
                                                        (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_DEVICE,
        g_param_spec_string("device", "Device", "OpenVINO device for NN model. Supported values: CPU, GPU, GPU.<id>",
                            DEFAULT_DEVICE, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(gobject_class, PROP_MODEL_TYPE,
                                    g_param_spec_string("model-type", "Model Type", "3D detector model type",
                                                        DEFAULT_MODEL_TYPE,
                                                        (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(gobject_class, PROP_SCORE_THRESHOLD,
                                    g_param_spec_float("score-threshold", "Score Threshold",
                                                       "Drop detections below this score (0 keeps all postproc output)",
                                                       0.0, 1.0, DEFAULT_SCORE_THRESHOLD,
                                                       (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_NIREQ,
        g_param_spec_uint("nireq", "NIReq",
                          "Number of inference requests processed concurrently. Frames are kept in flight across "
                          "this many requests while output order is preserved. 0 derives the value from the "
                          "compiled network",
                          0, MAX_NIREQ, DEFAULT_NIREQ, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    gst_element_class_set_static_metadata(
        gstelement_class, "G3D Inference", "Filter/Analyzer",
        "Runs PointPillars inference on LiDAR point clouds and attaches tensor metadata", "Intel Corporation");

    gst_element_class_add_static_pad_template(gstelement_class, &sink_template);
    gst_element_class_add_static_pad_template(gstelement_class, &src_template);

    base_transform_class->start = GST_DEBUG_FUNCPTR(gst_g3d_inference_start);
    base_transform_class->stop = GST_DEBUG_FUNCPTR(gst_g3d_inference_stop);
    base_transform_class->transform_ip = GST_DEBUG_FUNCPTR(gst_g3d_inference_transform_ip);
    base_transform_class->generate_output = GST_DEBUG_FUNCPTR(gst_g3d_inference_generate_output);
    base_transform_class->sink_event = GST_DEBUG_FUNCPTR(gst_g3d_inference_sink_event);
    base_transform_class->transform_caps = GST_DEBUG_FUNCPTR(gst_g3d_inference_transform_caps);
}

static void gst_g3d_inference_init(GstG3DInference *filter) {
    filter->config = NULL;
    filter->device = g_strdup(DEFAULT_DEVICE);
    filter->model_type = g_strdup(DEFAULT_MODEL_TYPE);
    filter->score_threshold = DEFAULT_SCORE_THRESHOLD;
    filter->nireq = DEFAULT_NIREQ;
    filter->initialized = FALSE;
    filter->runtime = NULL;
    filter->async_state = new G3DInferenceAsyncState();

    g_mutex_init(&filter->mutex);
    gst_base_transform_set_in_place(GST_BASE_TRANSFORM(filter), TRUE);
}

static void gst_g3d_inference_finalize(GObject *object) {
    GstG3DInference *filter = GST_G3D_INFERENCE(object);

    delete get_runtime(filter);
    filter->runtime = NULL;
    delete get_async_state(filter);
    filter->async_state = NULL;

    g_clear_pointer(&filter->config, g_free);
    g_clear_pointer(&filter->device, g_free);
    g_clear_pointer(&filter->model_type, g_free);
    g_mutex_clear(&filter->mutex);

    G_OBJECT_CLASS(gst_g3d_inference_parent_class)->finalize(object);
}

static void gst_g3d_inference_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) {
    GstG3DInference *filter = GST_G3D_INFERENCE(object);

    switch (prop_id) {
    case PROP_CONFIG:
        g_free(filter->config);
        filter->config = g_value_dup_string(value);
        break;
    case PROP_DEVICE:
        g_free(filter->device);
        filter->device = g_value_dup_string(value);
        break;
    case PROP_MODEL_TYPE:
        g_free(filter->model_type);
        filter->model_type = g_value_dup_string(value);
        break;
    case PROP_SCORE_THRESHOLD:
        filter->score_threshold = g_value_get_float(value);
        break;
    case PROP_NIREQ:
        filter->nireq = g_value_get_uint(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void gst_g3d_inference_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) {
    GstG3DInference *filter = GST_G3D_INFERENCE(object);

    switch (prop_id) {
    case PROP_CONFIG:
        g_value_set_string(value, filter->config);
        break;
    case PROP_DEVICE:
        g_value_set_string(value, filter->device);
        break;
    case PROP_MODEL_TYPE:
        g_value_set_string(value, filter->model_type);
        break;
    case PROP_SCORE_THRESHOLD:
        g_value_set_float(value, filter->score_threshold);
        break;
    case PROP_NIREQ:
        g_value_set_uint(value, filter->nireq);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static gboolean gst_g3d_inference_start(GstBaseTransform *trans) {
    GstG3DInference *filter = GST_G3D_INFERENCE(trans);

    if (!filter->config || !*filter->config) {
        GST_ELEMENT_ERROR(filter, RESOURCE, SETTINGS, ("Property 'config' is required"), (nullptr));
        return FALSE;
    }

    if (g_ascii_strcasecmp(filter->model_type, DEFAULT_MODEL_TYPE) != 0) {
        GST_ELEMENT_ERROR(filter, RESOURCE, SETTINGS, ("Unsupported model type: %s", filter->model_type), (nullptr));
        return FALSE;
    }

    if (!is_supported_device(filter->device)) {
        GST_ELEMENT_ERROR(filter, RESOURCE, SETTINGS,
                          ("Unsupported device: %s. Supported values: CPU, GPU, GPU.<id>",
                           filter->device ? filter->device : "<null>"),
                          (nullptr));
        return FALSE;
    }

    try {
        /* Held by unique_ptr until load() has succeeded: load() compiles the
         * models and may throw, and the raw pointer would otherwise leak because
         * ownership has not been handed to filter->runtime yet. */
        auto runtime = std::make_unique<PointPillarsRuntime>();
        runtime->load(filter->config, filter->device ? filter->device : DEFAULT_DEVICE, filter->nireq);
        delete get_runtime(filter);
        filter->initialized = TRUE;

        G3DInferenceAsyncState *state = get_async_state(filter);
        state->flushing.store(false);
        state->last_flow.store(GST_FLOW_OK);

        GST_INFO_OBJECT(filter, "Loaded PointPillars runtime with config=%s device=%s nireq=%zu", filter->config,
                        filter->device ? filter->device : DEFAULT_DEVICE, runtime->concurrency());
        filter->runtime = runtime.release();
        return TRUE;
    } catch (const std::exception &e) {
        GST_ELEMENT_ERROR(filter, LIBRARY, INIT, ("Failed to initialize PointPillars runtime"), ("%s", e.what()));
        delete get_runtime(filter);
        filter->runtime = NULL;
        filter->initialized = FALSE;
        return FALSE;
    }
}

static gboolean gst_g3d_inference_stop(GstBaseTransform *trans) {
    GstG3DInference *filter = GST_G3D_INFERENCE(trans);
    G3DInferenceAsyncState *state = get_async_state(filter);

    /* Stop the workers first: they hold raw pointers to this element and to the
     * queued buffers, so nothing may outlive this point. Setting the flushing
     * flag keeps a worker from blocking in gst_pad_push() while we join it. */
    if (state)
        state->flushing.store(true);

    if (PointPillarsRuntime *runtime = get_runtime(filter))
        runtime->shutdown();

    if (state) {
        for (const PendingFramePtr &frame : state->queue.take_all())
            discard_frame(frame);
    }

    delete get_runtime(filter);
    filter->runtime = NULL;
    filter->initialized = FALSE;
    return TRUE;
}

/* Never called: generate_output is overridden and consumes the queued buffer
 * itself. Kept registered because GstBaseTransform's internal logic checks for
 * a transform_ip implementation when deciding it can work in place. */
static GstFlowReturn gst_g3d_inference_transform_ip(GstBaseTransform *trans, GstBuffer *buffer) {
    (void)buffer;
    GST_LOG_OBJECT(trans, "transform_ip (unused, inference runs asynchronously)");
    return GST_BASE_TRANSFORM_FLOW_DROPPED;
}

/* Runs on a worker thread. Executes the full voxel -> nn -> postproc chain for
 * one frame, attaches the resulting metadata, then releases every frame whose
 * turn has come. */
static void gst_g3d_inference_process(GstG3DInference *filter, InferChain &chain, const PendingFramePtr &frame,
                                      LidarMeta *lidar_meta, float score_threshold) {
    G3DInferenceAsyncState *state = get_async_state(filter);
    PointPillarsRuntime *runtime = get_runtime(filter);

    try {
        const float *points = reinterpret_cast<const float *>(frame->map_info.data);
        std::vector<float> detections = runtime->infer(chain, points, lidar_meta->lidar_point_count, score_threshold);

        GstAnalyticsRelationMeta *rmeta = gst_buffer_get_analytics_relation_meta(frame->buffer);
        if (!rmeta)
            rmeta = gst_buffer_add_analytics_relation_meta(frame->buffer);
        if (!rmeta)
            throw std::runtime_error("Failed to allocate GstAnalyticsRelationMeta");

        const size_t detection_count = emit_3d_od_mtds(rmeta, detections);
        lidar_meta->exit_g3dinference_timestamp = get_exit_g3dinference_timestamp(filter);

        GST_DEBUG_OBJECT(
            filter, "Attached %zu PointPillars 3D detections for frame_id=%zu exit_g3dinference_ts=%" GST_TIME_FORMAT,
            detection_count, lidar_meta->frame_id, GST_TIME_ARGS(lidar_meta->exit_g3dinference_timestamp));
    } catch (const std::exception &e) {
        frame->status = GST_FLOW_ERROR;
        frame->error = e.what();
    }

    /* Release so the metadata written above is visible to the thread that
     * eventually pushes this buffer. */
    frame->ready.store(true, std::memory_order_release);
    push_ready_frames(filter);

    /* Decremented only after push_ready_frames() has returned, so that an
     * in-flight count of zero really means no worker is touching any frame. */
    state->end_frame();
}

static GstFlowReturn gst_g3d_inference_generate_output(GstBaseTransform *trans, GstBuffer **outbuf) {
    GstG3DInference *filter = GST_G3D_INFERENCE(trans);
    G3DInferenceAsyncState *state = get_async_state(filter);

    /* Inference completes asynchronously, so nothing is handed back to
     * GstBaseTransform here. Buffers are pushed from the worker threads. */
    *outbuf = NULL;

    GstBuffer *buffer = trans->queued_buf;
    trans->queued_buf = NULL;
    if (!buffer)
        return GST_BASE_TRANSFORM_FLOW_DROPPED;

    /* Report an error raised by a worker for an earlier frame, so a failing
     * stream still tears the pipeline down. */
    const GstFlowReturn last_flow = state->last_flow.load();
    if (last_flow != GST_FLOW_OK) {
        gst_buffer_unref(buffer);
        return last_flow;
    }

    auto frame = std::make_shared<PendingFrame>();
    float score_threshold = DEFAULT_SCORE_THRESHOLD;
    LidarMeta *lidar_meta = nullptr;

    try {
        GMutexLockGuard lock(&filter->mutex);
        score_threshold = filter->score_threshold;

        if (!filter->initialized || !get_runtime(filter))
            throw std::runtime_error("Runtime is not initialized");

        buffer = gst_buffer_make_writable(buffer);
        frame->buffer = buffer;

        lidar_meta = get_lidar_meta(buffer);
        if (!lidar_meta)
            throw std::runtime_error("LidarMeta is missing from input buffer");

        /* Validated on the streaming thread so malformed input fails the
         * pipeline synchronously, as it did before inference went async. The
         * mapping is kept alive until the worker has finished reading the
         * points; push_ready_frames() unmaps it. */
        if (!gst_buffer_map(buffer, &frame->map_info, GST_MAP_READ))
            throw std::runtime_error("Failed to map input buffer");
        frame->mapped = true;

        const gsize expected_size = static_cast<gsize>(lidar_meta->lidar_point_count) * POINT_SIZE * sizeof(float);
        if (frame->map_info.size != expected_size)
            throw std::runtime_error("Input payload size does not match LidarMeta point count");
    } catch (const std::exception &e) {
        GST_ELEMENT_ERROR(filter, STREAM, FAILED, ("Failed to process LiDAR buffer"), ("%s", e.what()));
        frame->unmap();
        if (frame->buffer)
            gst_buffer_unref(frame->buffer);
        else
            gst_buffer_unref(buffer);
        frame->buffer = nullptr;
        return GST_FLOW_ERROR;
    }

    /* Queue before submitting so the output order matches the input order even
     * if the worker finishes before this thread returns. */
    state->queue.push(frame);
    state->begin_frame();

    if (!get_runtime(filter)->submit([filter, frame, lidar_meta, score_threshold](InferChain &chain) {
            gst_g3d_inference_process(filter, chain, frame, lidar_meta, score_threshold);
        })) {
        /* Pool is shutting down. Complete the frame so the queue can drain. */
        frame->status = GST_FLOW_FLUSHING;
        frame->ready.store(true, std::memory_order_release);
        push_ready_frames(filter);
        state->end_frame();
        return GST_FLOW_FLUSHING;
    }

    return GST_BASE_TRANSFORM_FLOW_DROPPED;
}

static gboolean gst_g3d_inference_sink_event(GstBaseTransform *trans, GstEvent *event) {
    GstG3DInference *filter = GST_G3D_INFERENCE(trans);
    G3DInferenceAsyncState *state = get_async_state(filter);

    switch (GST_EVENT_TYPE(event)) {
    case GST_EVENT_FLUSH_START:
        /* Downstream is about to reject buffers; let workers drop their results
         * instead of pushing them. Also release the streaming thread if it is
         * blocked submitting into a full queue, otherwise it could never reach
         * FLUSH_STOP. This event is delivered on a different thread, so it can
         * run while the streaming thread is blocked. */
        state->flushing.store(true);
        if (PointPillarsRuntime *runtime = get_runtime(filter))
            runtime->set_submit_unblocked(true);
        break;
    case GST_EVENT_FLUSH_STOP:
        /* Wait only for workers to stop touching their frames -- results are
         * dropped during a flush, so waiting for them to be pushed would hang.
         * Then discard whatever is left so the new segment starts empty. */
        wait_for_in_flight(filter);
        for (const PendingFramePtr &frame : state->queue.take_all())
            discard_frame(frame);
        state->flushing.store(false);
        state->last_flow.store(GST_FLOW_OK);
        if (PointPillarsRuntime *runtime = get_runtime(filter))
            runtime->set_submit_unblocked(false);
        break;
    case GST_EVENT_EOS:
        /* EOS must not overtake frames still being processed, otherwise the
         * tail of the stream is lost. */
        drain_pending(filter);
        break;
    default:
        break;
    }

    return GST_BASE_TRANSFORM_CLASS(gst_g3d_inference_parent_class)->sink_event(trans, event);
}

static GstCaps *gst_g3d_inference_transform_caps(GstBaseTransform *trans, GstPadDirection direction, GstCaps *caps,
                                                 GstCaps *filter_caps) {
    (void)trans;
    (void)direction;
    (void)caps;
    (void)filter_caps;

    return gst_caps_from_string("application/x-lidar");
}