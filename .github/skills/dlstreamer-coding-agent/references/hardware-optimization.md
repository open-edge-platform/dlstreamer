# Hardware Optimization Reference

Map Intel hardware platforms to optimal DL Streamer inference device settings and pipeline tuning.

## Intel Platform Identification

| Marketing Name | Code Name | Accelerators |
|----------------|-----------|--------------|
| Intel® Core™ Ultra Series 3 | Panther Lake | CPU, GPU (Xe3/Battlemage), NPU |
| Intel® Core™ Ultra Series 2 | Arrow Lake | CPU, GPU (Arc), NPU |
| Intel® Core™ Ultra Series 2 | Lunar Lake | CPU, GPU (Arc), NPU |
| Intel® Core™ Ultra Series 1 | Meteor Lake | CPU, GPU (Arc), NPU |
| Intel® Core™ (12th–14th Gen) | Alder Lake / Raptor Lake / Raptor Lake Refresh | CPU, GPU (Iris Xe) |
| Intel® Xeon® 6 (Granite Rapids) | Granite Rapids | CPU, AMX |

> **Note:** NPU availability depends on the specific SKU. Not all SKUs within a family have NPU.
> Verify with `ls /dev/accel/accel*` on the host system.

## Device Detection at Runtime

Use these checks in applications to detect available accelerators:

```python
import os

def detect_devices():
    """Detect available Intel accelerators on the system."""
    devices = ["CPU"]  # CPU is always available

    # GPU: check for render nodes
    if any(os.path.exists(f"/dev/dri/renderD{128 + i}") for i in range(16)):
        devices.append("GPU")

    # NPU: check for accel devices
    if any(os.path.exists(f"/dev/accel/accel{i}") for i in range(8)):
        devices.append("NPU")

    return devices
```

## Device Assignment Strategy

### Single-Model Pipelines

| Model Type | Preferred Device | Fallback | Rationale |
|------------|-----------------|----------|-----------|
| Object detection (YOLO, SSD, RT-DETR) | GPU | CPU | High compute, benefits from GPU parallelism |
| Classification (ResNet, ViT, CLIP) | GPU | NPU or CPU | Medium compute, GPU preferred for batch |
| OCR (PaddleOCR) | GPU | CPU | Medium compute |
| VLM (MiniCPM-V, Qwen2.5-VL) | GPU | CPU | High compute + memory, GPU required for practical throughput |
| Audio (Whisper) | CPU | GPU | Sequential nature suits CPU |

### Multi-Model Pipelines (load balancing)

When a pipeline has multiple inference elements, distribute across devices to avoid
contention and maximize throughput:

```
# Example: Detection on GPU, Classification on NPU
gvadetect model=detect.xml device=GPU ! queue !
gvaclassify model=classify.xml device=NPU ! queue !
```

| Pipeline Shape | Recommended Assignment | Notes |
|----------------|----------------------|-------|
| detect only | `device=GPU` | Full GPU utilization |
| detect → classify | detect=GPU, classify=NPU | Balance load across accelerators |
| detect → classify → VLM | detect=GPU, classify=NPU, VLM=GPU | VLM needs GPU; time-share with detect |
| detect → OCR | detect=GPU, OCR=NPU or CPU | OCR is lightweight |
| detect → track → VLM | detect=GPU, VLM=GPU | tracking is CPU-only, lightweight |

> **NPU suitability:** NPU excels at sustained, low-power inference for classification-sized
> models (e.g. ResNet, EfficientNet, MobileNet). Large detection or VLM models may not fit
> NPU memory constraints. Always verify with a test run.

### Batch Size Tuning

| Device | Recommended `batch-size` | Notes |
|--------|-------------------------|-------|
| GPU | 4–8 | Higher batch amortizes GPU kernel launch overhead |
| NPU | 1–2 | NPU processes sequentially; large batches don't help |
| CPU | 1–4 | Depends on core count; larger batch for more cores |

### CPU Thread Tuning

For CPU inference, OpenVINO respects `OMP_NUM_THREADS` and the inference
`nstreams` property. On systems with E-cores and P-cores (Hybrid architecture):

```python
# Let OpenVINO auto-configure threading (recommended)
# Just set device=CPU — OpenVINO detects hybrid topology automatically

# For explicit control (advanced):
os.environ["OMP_NUM_THREADS"] = str(num_p_cores)
```

## Platform-Specific Pipeline Adjustments

### Intel Core Ultra with NPU (Meteor Lake, Lunar Lake, Panther Lake)

```
# Leverage NPU for always-on classification while GPU handles detection
gvadetect model=detect.xml device=GPU batch-size=4 ! queue !
gvaclassify model=classify.xml device=NPU batch-size=1 ! queue !
```

Docker requirements: mount `/dev/dri` (GPU) **and** `/dev/accel` (NPU), plus `--group-add` for
render and accel groups. See the canonical Docker run command in **SKILL.md → Step 4**.

### Intel Xeon (Server / Edge Server)

Server platforms typically have no GPU or NPU — use CPU with higher batch sizes
and stream parallelism:

```
gvadetect model=detect.xml device=CPU batch-size=8 ! queue !
```

For multi-stream workloads on Xeon, run multiple pipeline instances rather than
increasing batch size beyond 8.

### Intel Arc Discrete GPU

Discrete GPUs have dedicated VRAM — all inference elements can target GPU:

```
gvadetect model=detect.xml device=GPU batch-size=8 ! queue !
gvaclassify model=classify.xml device=GPU batch-size=8 ! queue !
```

## Weight Format by Device

| Device | Recommended Weight Format | Element Property |
|--------|--------------------------|------------------|
| GPU | FP16 or INT8 | (set at model export time) |
| NPU | INT8 | (set at model export time) |
| CPU | FP32 or INT8 | (set at model export time) |

> **INT4** is recommended only for VLM models via `optimum-cli --weight-format int4`.
> Detection and classification models should use INT8 for best quality/performance balance.

## Hardware-Aware Questions to Ask Users

When the user's target hardware is unknown, ask these questions to optimize the pipeline:

1. **Platform**: What Intel processor family? (Core Ultra, Xeon, Arc discrete GPU)
2. **Accelerators**: Is GPU available (`/dev/dri/renderD128`)? Is NPU available (`/dev/accel/accel0`)?
3. **Deployment**: Edge single-camera, edge multi-camera, or server/cloud?
4. **Power/Latency**: Optimize for throughput, latency, or power efficiency?

Map answers to device assignment:

| Priority | Single-camera Edge | Multi-camera Edge | Server |
|----------|-------------------|-------------------|--------|
| Throughput | detect=GPU, classify=NPU | per-camera pipeline, detect=GPU | detect=CPU batch-size=8 |
| Latency | detect=GPU batch-size=1 | detect=GPU batch-size=1 | detect=CPU batch-size=1 |
| Power | detect=NPU, classify=NPU | detect=NPU per camera | N/A |
