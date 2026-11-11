/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "bevfusion_runtime.h"

#include <nlohmann/json.hpp>
#include <openvino/openvino.hpp>
#include <openvino/runtime/intel_gpu/remote_properties.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <mutex>
#include <stdexcept>

namespace g3d_bevfusion {

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

// The sparse encoder extension keeps its OpenCL context and scratch buffers in
// process-wide statics, so calls from different runtimes must not overlap.
std::mutex &sparse_encoder_mutex() {
    static std::mutex m;
    return m;
}

json read_json(const fs::path &path) {
    std::ifstream f(path);
    if (!f)
        throw std::runtime_error("cannot open " + path.string());
    return json::parse(f);
}

fs::path resolve(const fs::path &base, const std::string &p) {
    fs::path path(p);
    return path.is_absolute() ? path : base / path;
}

template <typename T>
void expect(const json &cfg, const char *key, const T &want, const fs::path &file) {
    if (!cfg.contains(key) || cfg.at(key).get<T>() != want)
        throw std::runtime_error(file.string() + ": '" + key + "' does not match what this element was built for");
}

// Fills a fixed-size parameter from a JSON array of exactly that length.
template <typename T, size_t N>
void read_array(const json &value, const char *key, std::array<T, N> &out, const fs::path &file) {
    const auto v = value.get<std::vector<T>>();
    if (v.size() != N)
        throw std::runtime_error(file.string() + ": '" + key + "' must have " + std::to_string(N) + " values");
    std::copy_n(v.begin(), N, out.begin());
}

ModelParams load_model_params(const fs::path &file) {
    const json cfg = read_json(file);
    expect(cfg, "image_size", std::vector<int>{kImageH, kImageW}, file);
    expect(cfg, "feature_size", std::vector<int>{kFeatH, kFeatW}, file);
    expect(cfg, "num_cameras", kNumCameras, file);
    expect(cfg, "num_classes", kNumClasses, file);
    expect(cfg.at("depth"), "D", kDepthBins, file);
    expect(cfg.at("depth"), "C", kContextChannels, file);

    ModelParams p;
    p.resize_lim = cfg.at("resize_lim").at(0).get<double>();
    read_array(cfg.at("image_normalize").at("mean"), "mean", p.mean, file);
    read_array(cfg.at("image_normalize").at("std"), "std", p.std, file);
    read_array(cfg.at("depth").at("dbound"), "dbound", p.dbound, file);
    read_array(cfg.at("point_cloud_range"), "point_cloud_range", p.point_cloud_range, file);
    read_array(cfg.at("voxel_size"), "voxel_size", p.voxel_size, file);
    const json &post = cfg.at("post_processing");
    read_array(post.at("post_center_range"), "post_center_range", p.post_center_range, file);
    p.out_size_factor = post.at("out_size_factor").get<int>();
    p.class_names = cfg.at("class_names").get<std::vector<std::string>>();
    return p;
}

// Minimal .npy writer for the dump hook (C-order, little endian).
void write_npy(const fs::path &path, const void *data, const char *descr, size_t elem_size,
               const std::vector<size_t> &shape) {
    std::string dims;
    size_t count = 1;
    for (size_t d : shape) {
        dims += std::to_string(d) + ",";
        count *= d;
    }
    if (shape.size() > 1)
        dims.pop_back();
    std::string header = "{'descr': '" + std::string(descr) + "', 'fortran_order': False, 'shape': (" + dims + "), }";
    header.append(63 - (10 + header.size()) % 64, ' ');
    header.push_back('\n');
    std::ofstream f(path, std::ios::binary);
    const uint16_t len = static_cast<uint16_t>(header.size());
    f.write("\x93NUMPY\x01\x00", 8);
    f.write(reinterpret_cast<const char *>(&len), 2);
    f.write(header.data(), header.size());
    f.write(static_cast<const char *>(data), count * elem_size);
}

ov::Tensor host_tensor(ov::element::Type type, const ov::Shape &shape, void *data) {
    return ov::Tensor(type, shape, data);
}

} // namespace

struct Runtime::Impl {
    ov::Core core;
    ModelParams params;
    std::array<CameraCalibration, kNumCameras> cameras{};
    bool has_calibration = false;

    ov::InferRequest voxelize, sparse_encoder, geometry, camera_encoder, bev_pool, full_detection;

    // Host buffers, allocated once.
    std::vector<float> points = std::vector<float>(static_cast<size_t>(kMaxPoints) * kPointDims);
    std::vector<float> voxel_features = std::vector<float>(static_cast<size_t>(kMaxVoxels) * 5);
    std::vector<int32_t> voxel_coords = std::vector<int32_t>(static_cast<size_t>(kMaxVoxels) * 4);
    std::vector<float> lidar_bev = std::vector<float>(static_cast<size_t>(kLidarBevC) * kLidarBevH * kLidarBevW);
    std::vector<float> camera_bev =
        std::vector<float>(static_cast<size_t>(kContextChannels) * kCameraBevH * kCameraBevW);
    std::vector<float> images = std::vector<float>(static_cast<size_t>(kNumCameras) * 3 * kImageH * kImageW);
    std::vector<float> depth = std::vector<float>(static_cast<size_t>(kNumCameras) * kImageH * kImageW);
    std::vector<float> geometry_combined = std::vector<float>(kNumCameras * 14);
    std::vector<float> geometry_rotation = std::vector<float>(kNumCameras * 9);
    // bev_pool's int32 accumulator: BEVPoolScatter adds into it and
    // BEVPoolConvert resets it after reading, so it stays zeroed between frames.
    std::vector<int32_t> bev_accumulator;

    // With every model on OpenVINO's default GPU context, the tensors passed
    // from one model to the next stay in device memory instead of making a
    // round trip through the host (about 120 MB per frame).
    bool device_resident = false;
    ov::RemoteTensor dev_depth_logits, dev_context, dev_geometry, dev_accumulator, dev_camera_bev, dev_zero_camera_bev,
        dev_lidar_bev;

    std::string dump_dir;
    bool dump_tensors = false;
    uint64_t frame = 0;

    void dump(const char *tag, const ov::Tensor &tensor) {
        if (dump_dir.empty() || !dump_tensors)
            return;
        ov::Tensor t = tensor;
        if (tensor.is<ov::RemoteTensor>()) {
            t = ov::Tensor(tensor.get_element_type(), tensor.get_shape());
            tensor.as<ov::RemoteTensor>().copy_to(t);
        }
        const ov::Shape &s = t.get_shape();
        const std::vector<size_t> shape(s.begin(), s.end());
        const fs::path path = fs::path(dump_dir) / (std::string(tag) + "_" + std::to_string(frame) + ".npy");
        if (t.get_element_type() == ov::element::f32)
            write_npy(path, t.data(), "<f4", 4, shape);
        else if (t.get_element_type() == ov::element::i32)
            write_npy(path, t.data(), "<i4", 4, shape);
        else if (t.get_element_type() == ov::element::i64)
            write_npy(path, t.data(), "<i8", 8, shape);
    }

    void run_lidar(const FrameInput &frame_in) {
        const int n = pack_points(frame_in.points, frame_in.num_points, frame_in.point_stride, points.data());
        int32_t num_points = n;
        ov::Tensor pts = host_tensor(ov::element::f32, {kMaxPoints, kPointDims}, points.data());
        voxelize.set_input_tensor(0, pts);
        voxelize.set_input_tensor(1, host_tensor(ov::element::i32, {1}, &num_points));
        voxelize.infer();
        dump("points", pts);

        int32_t num_voxels =
            unpack_voxels(voxelize.get_output_tensor(0).data<float>(), voxel_features.data(), voxel_coords.data());
        ov::Tensor feat = host_tensor(ov::element::f32, {kMaxVoxels, 5}, voxel_features.data());
        ov::Tensor coords = host_tensor(ov::element::i32, {kMaxVoxels, 4}, voxel_coords.data());
        dump("voxel_features", feat);
        dump("voxel_coords", coords);

        std::lock_guard<std::mutex> lock(sparse_encoder_mutex());
        sparse_encoder.set_input_tensor(0, feat);
        sparse_encoder.set_input_tensor(1, coords);
        sparse_encoder.set_input_tensor(2, host_tensor(ov::element::i32, {1}, &num_voxels));
        sparse_encoder.infer();
        const ov::Tensor out = sparse_encoder.get_output_tensor(0);
        // Uploaded here, on the lidar thread, so the head does not wait for it.
        if (device_resident)
            dev_lidar_bev.copy_from(out);
        else
            std::copy_n(out.data<float>(), lidar_bev.size(), lidar_bev.data());
        dump("lidar_bev", out);
    }

    void run_camera(const FrameInput &frame_in) {
        std::array<Mat4, kNumCameras> aug{}, l2i{};
        const size_t image_size = static_cast<size_t>(3) * kImageH * kImageW;
        // One thread per camera: about 5 ms of resizing and normalising on
        // the camera branch's critical path otherwise.
        std::array<std::future<Mat4>, kNumCameras> pending;
        for (int c = 0; c < kNumCameras; c++)
            pending[c] = std::async(std::launch::async, [&, c] {
                return preprocess_image((*frame_in.cameras)[c], params, images.data() + c * image_size);
            });
        for (int c = 0; c < kNumCameras; c++) {
            aug[c] = pending[c].get();
            l2i[c] = lidar2image(cameras[c]);
        }
        depth_maps(frame_in.points, frame_in.num_points, frame_in.point_stride, l2i, aug, params, depth.data());
        pack_geometry(aug, cameras, geometry_combined.data(), geometry_rotation.data());

        ov::Tensor img = host_tensor(ov::element::f32, {kNumCameras, 3, kImageH, kImageW}, images.data());
        ov::Tensor dep = host_tensor(ov::element::f32, {kNumCameras, 1, kImageH, kImageW}, depth.data());
        dump("images", img);
        dump("depth_maps", dep);

        geometry.set_input_tensor(0, host_tensor(ov::element::f32, {kNumCameras, 14}, geometry_combined.data()));
        geometry.set_input_tensor(1, host_tensor(ov::element::f32, {kNumCameras, 3, 3}, geometry_rotation.data()));
        geometry.infer();
        camera_encoder.set_input_tensor(0, img);
        camera_encoder.set_input_tensor(1, dep);
        camera_encoder.infer();

        if (!device_resident) {
            bev_pool.set_input_tensor(0, camera_encoder.get_output_tensor(0)); // depth logits
            bev_pool.set_input_tensor(1, camera_encoder.get_output_tensor(1)); // context features
            bev_pool.set_input_tensor(2, geometry.get_output_tensor(0));
        }
        bev_pool.infer();
        const ov::Tensor out = bev_pool.get_output_tensor(0);
        if (!device_resident)
            std::copy_n(out.data<float>(), camera_bev.size(), camera_bev.data());
        dump("geometry", geometry.get_output_tensor(0));
        dump("depth_logits", camera_encoder.get_output_tensor(0));
        dump("context", camera_encoder.get_output_tensor(1));
        dump("camera_bev", out);
    }

    // Binds the camera branch and both inputs of the head to device tensors,
    // once. Only valid when the dense models share the default GPU context
    // with voxelize and bev_pool, i.e. device is "GPU".
    void bind_device_tensors() {
        ov::RemoteContext ctx = core.get_default_context("GPU");
        const ov::AnyMap usm_device{ov::intel_gpu::shared_mem_type(ov::intel_gpu::SharedMemType::USM_DEVICE_BUFFER)};
        auto make = [&](const ov::Output<const ov::Node> &port) {
            return ctx.create_tensor(port.get_element_type(), port.get_shape(), usm_device);
        };
        const ov::CompiledModel encoder = camera_encoder.get_compiled_model();
        const ov::CompiledModel pool = bev_pool.get_compiled_model();
        dev_depth_logits = make(encoder.output(0));
        dev_context = make(encoder.output(1));
        dev_geometry = make(geometry.get_compiled_model().output(0));
        dev_accumulator = make(pool.input("bev_accumulator"));
        dev_camera_bev = make(pool.output(0));
        dev_zero_camera_bev = make(full_detection.get_compiled_model().input(0));
        dev_lidar_bev = make(full_detection.get_compiled_model().input(1));

        // The accumulator must start zeroed; bevpool_convert keeps it zeroed.
        dev_accumulator.copy_from(host_tensor(ov::element::i32, dev_accumulator.get_shape(), bev_accumulator.data()));
        std::fill(camera_bev.begin(), camera_bev.end(), 0.0f);
        dev_zero_camera_bev.copy_from(
            host_tensor(ov::element::f32, dev_zero_camera_bev.get_shape(), camera_bev.data()));
        // The host buffers they replace are not used on this path.
        std::vector<int32_t>().swap(bev_accumulator);
        std::vector<float>().swap(camera_bev);
        std::vector<float>().swap(lidar_bev);

        camera_encoder.set_output_tensor(0, dev_depth_logits);
        camera_encoder.set_output_tensor(1, dev_context);
        geometry.set_output_tensor(0, dev_geometry);
        bev_pool.set_input_tensor(0, dev_depth_logits);
        bev_pool.set_input_tensor(1, dev_context);
        bev_pool.set_input_tensor(2, dev_geometry);
        bev_pool.set_tensor("bev_accumulator", dev_accumulator);
        bev_pool.set_output_tensor(0, dev_camera_bev);
        full_detection.set_input_tensor(1, dev_lidar_bev);
        device_resident = true;
    }

    HeadOutputs run_head(bool with_cameras) {
        if (device_resident) {
            full_detection.set_input_tensor(0, with_cameras ? dev_camera_bev : dev_zero_camera_bev);
        } else {
            full_detection.set_input_tensor(
                0, host_tensor(ov::element::f32, {1, kContextChannels, kCameraBevH, kCameraBevW}, camera_bev.data()));
            full_detection.set_input_tensor(
                1, host_tensor(ov::element::f32, {1, kLidarBevC, kLidarBevH, kLidarBevW}, lidar_bev.data()));
        }
        full_detection.infer();

        auto floats = [&](size_t i) {
            const ov::Tensor t = full_detection.get_output_tensor(i);
            return std::vector<float>(t.data<float>(), t.data<float>() + t.get_size());
        };
        HeadOutputs h;
        h.center = floats(0);
        h.height = floats(1);
        h.dim = floats(2);
        h.rot = floats(3);
        h.vel = floats(4);
        h.heatmap = floats(5);
        h.query_heatmap_score = floats(6);
        const ov::Tensor cls = full_detection.get_output_tensor(9);
        h.top_proposals_class.resize(cls.get_size());
        if (cls.get_element_type() == ov::element::i64)
            std::copy_n(cls.data<int64_t>(), cls.get_size(), h.top_proposals_class.begin());
        else
            std::copy_n(cls.data<int32_t>(), cls.get_size(), h.top_proposals_class.begin());
        static const char *names[] = {"head_center",
                                      "head_height",
                                      "head_dim",
                                      "head_rot",
                                      "head_vel",
                                      "head_heatmap",
                                      "head_query_heatmap_score",
                                      "head_dense_heatmap",
                                      "head_top_proposals_index",
                                      "head_top_proposals_class"};
        for (size_t i = 0; i < 10; i++)
            dump(names[i], full_detection.get_output_tensor(i));
        return h;
    }
};

Runtime::Runtime(const std::string &config_path, const std::string &device) : impl_(std::make_unique<Impl>()) {
    const fs::path cfg_path = fs::absolute(config_path);
    const json cfg = read_json(cfg_path);
    const fs::path base = cfg_path.parent_path();
    const fs::path model_dir = resolve(base, cfg.at("model_dir").get<std::string>());
    impl_->params = load_model_params(model_dir / "config.json");

    ov::Core &core = impl_->core;
    for (const auto &ext : cfg.at("extensions"))
        core.add_extension(resolve(base, ext.get<std::string>()).string());
    // voxelize and bev_pool are GPU custom layers: their kernels are described
    // by this file, and they always run on the GPU.
    core.set_property(
        "GPU", ov::AnyMap{{"CONFIG_FILE", resolve(base, cfg.at("gpu_custom_layers").get<std::string>()).string()}});

    const bool on_gpu = device.rfind("GPU", 0) == 0;
    ov::AnyMap dense_cfg{ov::hint::performance_mode(ov::hint::PerformanceMode::LATENCY)};
    if (on_gpu)
        dense_cfg.emplace(ov::hint::inference_precision.name(), ov::element::f16);
    const ov::AnyMap custom_cfg{ov::hint::performance_mode(ov::hint::PerformanceMode::LATENCY)};
    auto compile = [&](const char *name, const std::string &dev, const ov::AnyMap &props) {
        return core.compile_model((model_dir / (std::string(name) + ".xml")).string(), dev, props)
            .create_infer_request();
    };
    impl_->voxelize = compile("voxelize", "GPU", custom_cfg);
    // The scatter needs an accumulator that holds zeros on entry. As its own
    // output it could get a reused GPU pool buffer holding another layer's
    // data, so the model takes it as an input instead. Checked before
    // compiling: an older export does not compile against these kernels.
    const fs::path bev_pool_xml = model_dir / "bev_pool.xml";
    const std::shared_ptr<ov::Model> bev_pool_model = core.read_model(bev_pool_xml.string());
    const auto bev_pool_inputs = bev_pool_model->inputs();
    const auto accumulator =
        std::find_if(bev_pool_inputs.begin(), bev_pool_inputs.end(),
                     [](const ov::Output<ov::Node> &port) { return port.get_names().count("bev_accumulator"); });
    if (accumulator == bev_pool_inputs.end())
        throw std::runtime_error(bev_pool_xml.string() + " has no bev_accumulator input; export the model again with "
                                                         "g3dbevfusion_prepare_model.sh");
    const ov::Shape accumulator_shape = accumulator->get_shape();
    impl_->bev_pool = core.compile_model(bev_pool_model, "GPU", custom_cfg).create_infer_request();
    impl_->bev_accumulator.assign(ov::shape_size(accumulator_shape), 0);
    impl_->bev_pool.set_tensor("bev_accumulator",
                               host_tensor(ov::element::i32, accumulator_shape, impl_->bev_accumulator.data()));
    impl_->sparse_encoder = compile("sparse_encoder", "CPU", {});
    impl_->geometry = compile("geometry", device, dense_cfg);
    impl_->camera_encoder = compile("camera_encoder_b6", device, dense_cfg);
    impl_->full_detection = compile("full_detection", device, dense_cfg);
    if (device == "GPU")
        impl_->bind_device_tensors();
}

Runtime::~Runtime() = default;

void Runtime::set_calibration(const std::array<CameraCalibration, kNumCameras> &cameras) {
    impl_->cameras = cameras;
    impl_->has_calibration = true;
}

const ModelParams &Runtime::params() const {
    return impl_->params;
}

void Runtime::set_dump_dir(const std::string &dir, bool tensors) {
    impl_->dump_dir = dir;
    impl_->dump_tensors = tensors;
    if (!dir.empty())
        fs::create_directories(dir);
}

std::vector<Box3D> Runtime::infer(const FrameInput &frame, float score_threshold) {
    if (!frame.points || frame.point_stride < 4)
        throw std::invalid_argument("lidar points are required (at least x, y, z, intensity per point)");

    if (frame.cameras) {
        if (!impl_->has_calibration)
            throw std::logic_error("camera calibration must be set before running with cameras");
        // The two branches share no buffers and only read the frame, so the
        // lidar branch runs on its own thread while this one runs the camera
        // branch. The future's destructor joins it if run_camera throws.
        std::future<void> lidar = std::async(std::launch::async, [&] { impl_->run_lidar(frame); });
        impl_->run_camera(frame);
        lidar.get();
    } else {
        impl_->run_lidar(frame);
        // Lidar-only: the fused head sees an empty camera BEV.
        if (!impl_->device_resident)
            std::fill(impl_->camera_bev.begin(), impl_->camera_bev.end(), 0.0f);
    }
    std::vector<Box3D> boxes = decode(impl_->run_head(frame.cameras.has_value()), impl_->params, score_threshold);
    if (!impl_->dump_dir.empty()) {
        std::vector<float> rows;
        for (const Box3D &b : boxes)
            rows.insert(rows.end(), {b.x, b.y, b.z_bottom, b.dx, b.dy, b.dz, b.yaw, b.vx, b.vy, b.score,
                                     static_cast<float>(b.label)});
        char name[32];
        std::snprintf(name, sizeof(name), "boxes_%06llu.npy", static_cast<unsigned long long>(impl_->frame));
        write_npy(fs::path(impl_->dump_dir) / name, rows.data(), "<f4", 4, {boxes.size(), 11});
    }
    impl_->frame++;
    return boxes;
}

} // namespace g3d_bevfusion
