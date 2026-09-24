#!/usr/bin/env python3
#*******************************************************************************
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ******************************************************************************

"""
Standalone MonoDETR -> OpenVINO converter (starts from the ORIGINAL upstream repo).

What it does, end to end:
  1. Clones https://github.com/ZrrSkywalker/MonoDETR (once) into --repo.
  2. Applies -- at runtime, without editing any upstream file -- the minimal
     changes this fork needs to trace MonoDETR without CUDA under torch 2.x:
       a. a dummy `MultiScaleDeformableAttention` module + a pure-PyTorch
          MSDA op, so the un-built CUDA extension is not required and the graph
          is transparent to torch.jit.trace / ov.convert_model;
       b. torch-2.x back-compat shims for the custom MultiheadAttention version
          guards (`_LinearWithBias`, `torch._overrides`);
       c. a forward split on MonoDETR (`forward_backbone` / `build_masks_pos` /
          `forward_head`) plus the fp16 depth-reciprocal clamp.
  3. Builds the model, loads --ckpt, and exports ONE mixed-precision IR:
     ResNet backbone in f32 (its layer4 activations reach ~1e5 and overflow
     fp16), transformer/head in real f16, stitched with a Convert at each
     feature-map boundary.

Run the produced IR with EXECUTION_MODE_HINT=ACCURACY (DLStreamer:
`ie-config=EXECUTION_MODE_HINT=ACCURACY`); the default PERFORMANCE mode forces
everything to f16 and re-overflows the backbone to NaN.

Example:
    python convert_monodetr_ov.py --ckpt checkpoint_best.pth --output monodetr.xml

If --ckpt is omitted, the upstream checkpoint is downloaded from Google Drive
(requires `gdown`). Run this OUTSIDE an existing MonoDETR checkout so
`import lib/` resolves to the freshly cloned upstream tree.
"""

# Imports are intentionally deferred until the cloned upstream MonoDETR repo is
# on sys.path (and heavy deps are loaded lazily), so allow function-level imports.
# pylint: disable=import-outside-toplevel

import argparse
import os
import subprocess
import sys
import types

import numpy as np
import torch

UPSTREAM_URL = "https://github.com/ZrrSkywalker/MonoDETR.git"

# Upstream-hosted checkpoint (Google Drive) used when --ckpt is omitted; this is
# the first "newer results" row from the upstream README.
_DEFAULT_CKPT_GDRIVE_ID = "1d8fbAt-CQF-IN8UEHuw3NimmfONhH6iA"
_DEFAULT_CKPT_NAME = "monodetr_upstream.pth"

# ImageNet normalization + network canvas (matches MonoDETR KITTI preprocessing).
_MEAN = np.array([0.485, 0.456, 0.406], dtype=np.float32)
_STD = np.array([0.229, 0.224, 0.225], dtype=np.float32)
_IN_W, _IN_H = 1280, 384
# A representative KITTI P2 (used only to trace / sanity-check when --calib is omitted).
_DEFAULT_P2 = np.array([[721.5377, 0.0, 609.5593, 44.85728],
                        [0.0, 721.5377, 172.854, 0.2163791],
                        [0.0, 0.0, 1.0, 0.002745884]], dtype=np.float32)
_DEFAULT_WH = np.array([1242.0, 375.0], dtype=np.float32)  # KITTI original (W, H)


# ---------------------------------------------------------------------------
# 1. clone upstream
# ---------------------------------------------------------------------------
def clone_upstream(repo, ref=None):
    """Clone (or reuse) the upstream MonoDETR repo, optionally at a given git ref."""
    if not os.path.isdir(os.path.join(repo, ".git")):
        print(f"[info] cloning {UPSTREAM_URL} -> {repo}")
        subprocess.run(["git", "clone", "--depth", "1", UPSTREAM_URL, repo], check=True)
    else:
        print(f"[info] using existing clone at {repo}")
    if ref:
        subprocess.run(["git", "-C", repo, "fetch", "--depth", "1", "origin", ref], check=True)
        subprocess.run(["git", "-C", repo, "checkout", ref], check=True)
    return os.path.abspath(repo)


def download_checkpoint(dest):
    """Fetch the upstream MonoDETR checkpoint from Google Drive (cached)."""
    if os.path.exists(dest) and os.path.getsize(dest) > 1_000_000:
        print(f"[info] using cached checkpoint {dest}")
        return dest
    try:
        import gdown  # pylint: disable=import-outside-toplevel
    except ImportError as exc:
        raise SystemExit(
            "Downloading the default checkpoint requires gdown "
            "(`pip install gdown`), or pass --ckpt with a local .pth.") from exc
    os.makedirs(os.path.dirname(os.path.abspath(dest)) or ".", exist_ok=True)
    print(f"[info] downloading MonoDETR checkpoint from Google Drive -> {dest}")
    gdown.download(id=_DEFAULT_CKPT_GDRIVE_ID, output=dest, quiet=False)
    if not (os.path.exists(dest) and os.path.getsize(dest) > 1_000_000):
        raise SystemExit(
            "Checkpoint download failed (Google Drive quota / bad id?). "
            "Download it manually from the upstream README and pass --ckpt.")
    return dest


# ---------------------------------------------------------------------------
# 2a. pure-PyTorch MSDA op (traceable, CPU) + import shims
# ---------------------------------------------------------------------------
def _ms_deform_attn_core_pytorch(value, value_spatial_shapes, sampling_locations, attention_weights):  # pylint: disable=too-many-locals
    """Pure-PyTorch, CPU/traceable reference implementation of multi-scale deformable attention."""
    # short tensor-dim names below mirror the upstream Deformable-DETR reference
    # pylint: disable=invalid-name
    import torch.nn.functional as F
    N_, _, M_, D_ = value.shape
    _, Lq_, M_, L_, P_, _ = sampling_locations.shape
    value_list = value.split([int(H_) * int(W_) for H_, W_ in value_spatial_shapes], dim=1)
    sampling_grids = 2 * sampling_locations - 1
    sampling_value_list = []
    for lid_, (H_, W_) in enumerate(value_spatial_shapes):
        H_, W_ = int(H_), int(W_)
        value_l_ = value_list[lid_].flatten(2).transpose(1, 2).reshape(N_ * M_, D_, H_, W_)
        sampling_grid_l_ = sampling_grids[:, :, :, lid_].transpose(1, 2).flatten(0, 1)
        sampling_value_l_ = F.grid_sample(
            value_l_, sampling_grid_l_, mode="bilinear",
            padding_mode="zeros", align_corners=False)
        sampling_value_list.append(sampling_value_l_)
    attention_weights = attention_weights.transpose(1, 2).reshape(N_ * M_, 1, Lq_, L_ * P_)
    output = (torch.stack(sampling_value_list, dim=-2).flatten(-2) * attention_weights).sum(-1).view(
        N_, M_ * D_, Lq_)
    return output.transpose(1, 2).contiguous()


def install_import_shims():
    """Make the upstream ops import under torch 2.x without the CUDA extension."""
    # (a) satisfy `import MultiScaleDeformableAttention` (CUDA ext not built).
    sys.modules.setdefault(
        "MultiScaleDeformableAttention",
        types.ModuleType("MultiScaleDeformableAttention"))
    # (b) torch-2.x back-compat for the custom MultiheadAttention version guards,
    #     whichever branch they take.
    import importlib
    sys.modules.setdefault("torch._overrides", importlib.import_module("torch.overrides"))
    from torch.nn.modules import linear as _lin
    if not hasattr(_lin, "_LinearWithBias"):
        _lin._LinearWithBias = getattr(  # pylint: disable=protected-access
            _lin, "NonDynamicallyQuantizableLinear", torch.nn.Linear)


def override_msda_function():
    """Replace MSDeformAttnFunction with a plain, traceable class (no autograd,
    no CUDA) that calls the pure-PyTorch reference."""
    from lib.models.monodetr.ops.functions import ms_deform_attn_func as f
    from lib.models.monodetr.ops.modules import ms_deform_attn as m

    class MSDeformAttnFunction:  # pylint: disable=too-few-public-methods
        """Traceable stand-in for the upstream autograd MSDeformAttnFunction."""

        @staticmethod
        def apply(value, value_spatial_shapes, value_level_start_index,  # pylint: disable=too-many-arguments,too-many-positional-arguments
                  sampling_locations, attention_weights, im2col_step):
            """Match the upstream .apply() signature; forward to the pure-PyTorch core."""
            # value_level_start_index / im2col_step are unused by the pure-PyTorch path
            # pylint: disable=unused-argument
            return _ms_deform_attn_core_pytorch(
                value, value_spatial_shapes, sampling_locations, attention_weights)

    f.MSDeformAttnFunction = MSDeformAttnFunction
    m.MSDeformAttnFunction = MSDeformAttnFunction


# ---------------------------------------------------------------------------
# 2c. forward split (attached to upstream MonoDETR at runtime)
# ---------------------------------------------------------------------------
def install_forward_split():  # pylint: disable=too-many-statements
    """Attach the traced-friendly forward_backbone/build_masks_pos/forward_head split onto MonoDETR."""
    import torch.nn.functional as F
    from utils.misc import NestedTensor, inverse_sigmoid  # pylint: disable=no-name-in-module
    from lib.models.monodetr.monodetr import MonoDETR

    def forward_backbone(self, images):
        features, pos = self.backbone(images)
        srcs, masks = [], []
        for l, feat in enumerate(features):
            src, mask = feat.decompose()
            srcs.append(self.input_proj[l](src))
            masks.append(mask)
            assert mask is not None
        if self.num_feature_levels > len(srcs):
            _len_srcs = len(srcs)
            for l in range(_len_srcs, self.num_feature_levels):
                if l == _len_srcs:
                    src = self.input_proj[l](features[-1].tensors)
                else:
                    src = self.input_proj[l](srcs[-1])
                m = torch.zeros(src.shape[0], src.shape[2], src.shape[3]).to(torch.bool).to(src.device)
                mask = F.interpolate(m[None].float(), size=src.shape[-2:]).to(torch.bool)[0]
                pos_l = self.backbone[1](NestedTensor(src, mask)).to(src.dtype)
                srcs.append(src)
                masks.append(mask)
                pos.append(pos_l)
        return srcs, masks, pos

    def build_masks_pos(self, srcs):
        masks, pos = [], []
        for src in srcs:
            m = torch.zeros(src.shape[0], src.shape[2], src.shape[3]).to(torch.bool).to(src.device)
            masks.append(m)
            pos.append(self.backbone[1](NestedTensor(src, m)).to(src.dtype))
        return masks, pos

    def forward_head(self, srcs, masks, pos, calibs, img_sizes):  # pylint: disable=too-many-locals,too-many-arguments,too-many-positional-arguments
        if self.two_stage:
            query_embeds = None
        elif self.use_dab:
            tgt_embed = self.tgt_embed.weight[:self.num_queries]
            refanchor = self.refpoint_embed.weight[:self.num_queries]
            query_embeds = torch.cat((tgt_embed, refanchor), dim=1)
        elif self.two_stage_dino:
            query_embeds = None
        else:
            query_embeds = self.query_embed.weight[:self.num_queries]

        pred_depth_map_logits, depth_pos_embed, weighted_depth, depth_pos_embed_ip = \
            self.depth_predictor(srcs, masks[1], pos[1])

        hs, init_reference, inter_references, inter_references_dim, _, _ = \
            self.depthaware_transformer(
                srcs, masks, pos, query_embeds, depth_pos_embed, depth_pos_embed_ip)

        outputs_coords, outputs_classes = [], []
        outputs_3d_dims, outputs_depths, outputs_angles = [], [], []
        for lvl in range(hs.shape[0]):
            reference = init_reference if lvl == 0 else inter_references[lvl - 1]
            reference = inverse_sigmoid(reference)
            tmp = self.bbox_embed[lvl](hs[lvl])
            if reference.shape[-1] == 6:
                tmp += reference
            else:
                assert reference.shape[-1] == 2
                tmp[..., :2] += reference
            outputs_coord = tmp.sigmoid()
            outputs_coords.append(outputs_coord)
            outputs_classes.append(self.class_embed[lvl](hs[lvl]))
            size3d = inter_references_dim[lvl]
            outputs_3d_dims.append(size3d)

            box2d_height_norm = outputs_coord[:, :, 4] + outputs_coord[:, :, 5]
            box2d_height = torch.clamp(box2d_height_norm * img_sizes[:, 1:2], min=1.0)
            depth_geo = size3d[:, :, 0] / box2d_height * calibs[:, 0, 0].unsqueeze(1)
            depth_reg = self.depth_embed[lvl](hs[lvl])
            outputs_center3d = ((outputs_coord[..., :2] - 0.5) * 2).unsqueeze(2).detach()
            depth_map = F.grid_sample(
                weighted_depth.unsqueeze(1), outputs_center3d,
                mode="bilinear", align_corners=True).squeeze(1)
            # fp16-safety: 1/(sigmoid+1e-6) reaches ~1e6 and overflows fp16.
            inv_depth = (1. / (depth_reg[:, :, 0:1].sigmoid() + 1e-6) - 1.).clamp(max=1e4)
            depth_ave = torch.cat(
                [(inv_depth + depth_geo.unsqueeze(-1) + depth_map) / 3, depth_reg[:, :, 1:2]], -1)
            outputs_depths.append(depth_ave)
            outputs_angles.append(self.angle_embed[lvl](hs[lvl]))

        out = {
            "pred_logits": torch.stack(outputs_classes)[-1],
            "pred_boxes": torch.stack(outputs_coords)[-1],
            "pred_3d_dim": torch.stack(outputs_3d_dims)[-1],
            "pred_depth": torch.stack(outputs_depths)[-1],
            "pred_angle": torch.stack(outputs_angles)[-1],
            "pred_depth_map_logits": pred_depth_map_logits,
        }
        return out

    def forward(self, images, calibs, targets=None, img_sizes=None, dn_args=None):  # pylint: disable=too-many-arguments,too-many-positional-arguments,unused-argument
        srcs, masks, pos = self.forward_backbone(images)
        return self.forward_head(srcs, masks, pos, calibs, img_sizes)

    MonoDETR.forward_backbone = forward_backbone
    MonoDETR.build_masks_pos = build_masks_pos
    MonoDETR.forward_head = forward_head
    MonoDETR.forward = forward


# ---------------------------------------------------------------------------
# 3. build model + example inputs
# ---------------------------------------------------------------------------
def build_and_load(repo, ckpt):
    """Build the MonoDETR model from the upstream config and load the checkpoint weights."""
    import yaml
    from lib.helpers.model_helper import build_model

    cfg_path = os.path.join(repo, "configs", "monodetr.yaml")
    with open(cfg_path, encoding="utf-8") as fh:
        cfg = yaml.safe_load(fh)
    cfg["model"]["device"] = "cpu"
    # DDNLoss.__init__ calls torch.cuda.current_device(); unused at inference.
    torch.cuda.current_device = lambda: torch.device("cpu")

    model, _ = build_model(cfg["model"])
    state = torch.load(ckpt, map_location="cpu", weights_only=True)
    state_dict = state.get("model_state", state.get("state_dict", state))
    missing, unexpected = model.load_state_dict(state_dict, strict=False)
    print(f"[info] loaded checkpoint (missing={len(missing)}, unexpected={len(unexpected)})")
    return model.eval()


def preprocess_image(path):
    """Load an image and return a normalized NCHW tensor plus its original (W, H)."""
    from PIL import Image
    img = Image.open(path).convert("RGB")
    orig_wh = np.array(img.size, dtype=np.float32)  # (W, H)
    img = img.resize((_IN_W, _IN_H), Image.Resampling.BILINEAR)
    arr = (np.asarray(img, dtype=np.float32) / 255.0 - _MEAN) / _STD
    return torch.from_numpy(arr.transpose(2, 0, 1))[None], orig_wh


def load_p2(path):
    """Return the 3x4 KITTI P2 projection matrix from a calibration file, or a default."""
    if path is None:
        return _DEFAULT_P2.copy()
    with open(path, encoding="utf-8") as fh:
        for line in fh:
            if line.startswith("P2:"):
                vals = [float(x) for x in line.split()[1:]]
                return np.array(vals, dtype=np.float32).reshape(3, 4)
    raise ValueError(f"no 'P2:' line found in {path}")


def make_example(image, calib):
    """Build the (images, calibs, img_sizes) example tuple used to trace/convert the model."""
    if image:
        images, orig_wh = preprocess_image(image)
    else:
        print("[info] no --image given; tracing with a synthetic frame")
        images = torch.zeros(1, 3, _IN_H, _IN_W)
        orig_wh = _DEFAULT_WH.copy()
    calibs = torch.from_numpy(load_p2(calib))[None]
    img_sizes = torch.from_numpy(orig_wh)[None]
    return images, calibs, img_sizes


# ---------------------------------------------------------------------------
# 3. export the single mixed-precision IR
# ---------------------------------------------------------------------------
def export_mixed_ir(model, out_path, images, calibs, img_sizes, sanity=True):  # pylint: disable=too-many-locals,too-many-statements,too-many-arguments,too-many-positional-arguments
    """Trace the model and save a single mixed-precision (f32 backbone / f16 head) OpenVINO IR."""
    import openvino as ov
    from openvino import opset13
    from openvino.passes import ConvertFP32ToFP16, Manager

    class _BackboneWrap(torch.nn.Module):
        def __init__(self, m):
            super().__init__()
            self.m = m

        def forward(self, images):
            """Return the backbone src feature maps as a tuple."""
            return tuple(self.m.forward_backbone(images)[0])

    class _HeadWrap(torch.nn.Module):
        def __init__(self, m):
            super().__init__()
            self.m = m

        def forward(self, s0, s1, s2, s3, calibs, img_sizes):  # pylint: disable=too-many-arguments,too-many-positional-arguments
            """Run masks/pos build + head on the four backbone feature maps."""
            srcs = [s0, s1, s2, s3]
            masks, pos = self.m.build_masks_pos(srcs)
            out = self.m.forward_head(srcs, masks, pos, calibs, img_sizes)
            return (out["pred_logits"], out["pred_boxes"], out["pred_3d_dim"],
                    out["pred_depth"], out["pred_angle"])

    bw, hw = _BackboneWrap(model).eval(), _HeadWrap(model).eval()
    print("[info] tracing backbone + head ...")
    with torch.no_grad():
        ov_bb = ov.convert_model(bw, example_input=(images,))
        srcs = bw(images)
        ov_hd = ov.convert_model(hw, example_input=(*srcs, calibs, img_sizes))

    in_h, in_w = images.shape[2], images.shape[3]
    ov_bb.reshape({ov_bb.inputs[0].any_name: [-1, 3, in_h, in_w]})
    hr = {ov_hd.inputs[i].any_name: [-1, s.shape[1], s.shape[2], s.shape[3]]
          for i, s in enumerate(srcs)}
    hr[ov_hd.inputs[4].any_name] = [-1, 3, 4]
    hr[ov_hd.inputs[5].any_name] = [-1, 2]
    ov_hd.reshape(hr)

    # Convert ONLY the head subgraph to real f16 ops, then stitch it onto the
    # f32 backbone with a Convert at each src boundary.
    mgr = Manager()
    mgr.register_pass(ConvertFP32ToFP16())
    mgr.run_passes(ov_hd)

    bb_src_out = [r.input_value(0) for r in ov_bb.get_results()]
    hd_params = ov_hd.get_parameters()
    for i in range(4):
        src_out = bb_src_out[i]
        want = hd_params[i].get_element_type()
        if want != src_out.get_element_type():
            src_out = opset13.convert(src_out, want).output(0)
        for tgt in list(hd_params[i].output(0).get_target_inputs()):
            tgt.replace_source_output(src_out)

    combined = ov.Model(
        ov_hd.get_results(),
        [ov_bb.get_parameters()[0], hd_params[4], hd_params[5]],
        "monodetr")

    for out, name in zip(combined.outputs,
                         ("pred_logits", "pred_boxes", "pred_3d_dim",
                          "pred_depth", "pred_angle")):
        out.get_tensor().set_names({name})

    combined.set_rt_info("mono3d", ["model_info", "model_type"])
    combined.set_rt_info("standard", ["model_info", "resize_type"])
    combined.set_rt_info("True", ["model_info", "reverse_input_channels"])
    combined.set_rt_info("123.675 116.28 103.53", ["model_info", "mean_values"])
    combined.set_rt_info("58.395 57.12 57.375", ["model_info", "scale_values"])
    combined.set_rt_info("Pedestrian Car Cyclist", ["model_info", "labels"])
    combined.set_rt_info("ACCURACY", ["runtime_options", "EXECUTION_MODE_HINT"])

    ov.save_model(combined, out_path)
    print(f"[info] saved {out_path}")
    print("[info] run with EXECUTION_MODE_HINT=ACCURACY "
          "(DLStreamer: ie-config=EXECUTION_MODE_HINT=ACCURACY); the default "
          "PERFORMANCE mode overflows the f32 backbone to NaN.")

    if sanity:
        core = ov.Core()
        compiled = core.compile_model(combined, "CPU")
        o = compiled([images.numpy(), calibs.numpy(), img_sizes.numpy()])
        nan = sum(int(np.isnan(np.asarray(v)).sum()) for _, v in o.items())
        print("[info] CPU sanity:",
              [(i, list(v.shape)) for i, (_, v) in enumerate(o.items())],
              f"totalNaN={nan}")


# ---------------------------------------------------------------------------
def parse_args():
    """Parse command-line arguments."""
    p = argparse.ArgumentParser(description=__doc__.strip().splitlines()[0])
    p.add_argument("--ckpt", default=None,
                   help="MonoDETR checkpoint .pth (default: download from upstream Google Drive)")
    p.add_argument("--output", default="monodetr.xml", help="output IR path (.xml)")
    p.add_argument("--repo", default="MonoDETR_upstream", help="where to clone upstream")
    p.add_argument("--ref", default=None, help="git ref (commit/tag/branch) to check out")
    p.add_argument("--image", default=None, help="example image for tracing (optional)")
    p.add_argument("--calib", default=None, help="KITTI .txt calibration for tracing (optional)")
    p.add_argument("--no-sanity", action="store_true", help="skip the CPU sanity inference")
    return p.parse_args()


def main():
    """Clone upstream, patch it for CPU tracing, build the model and export the IR."""
    args = parse_args()
    repo = clone_upstream(args.repo, args.ref)
    ckpt = args.ckpt or download_checkpoint(os.path.join(repo, "chkpts", _DEFAULT_CKPT_NAME))

    sys.path.insert(0, repo)          # upstream lib/ + utils/ take precedence
    install_import_shims()            # torch-2.x / no-CUDA import fixes
    install_forward_split()           # attach forward_backbone/head to MonoDETR
    override_msda_function()          # traceable pure-PyTorch MSDA op

    model = build_and_load(repo, ckpt)
    images, calibs, img_sizes = make_example(args.image, args.calib)
    export_mixed_ir(model, os.path.abspath(args.output),
                    images, calibs, img_sizes, sanity=not args.no_sanity)


if __name__ == "__main__":
    main()
