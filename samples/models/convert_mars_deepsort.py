#!/usr/bin/env python3
# ==============================================================================
# Copyright (C) 2021-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

# ==============================================================================
# Mars-Small128 DeepSORT Model Converter to OpenVINO

# Uses official deep_sort_pytorch model architecture from:
# https://github.com/ZQPei/deep_sort_pytorch/blob/master/deep_sort/deep/model.py

# Generates FP32 and INT8 models optimized for DeepSORT tracking.

# Converts the ORIGINAL checkpoint (original_ckpt.t7):
# - 11MB model size
# - 32→64→128 channel progression
# - 625 person classes
# - 128-dimensional output (standard DeepSORT)
# ==============================================================================

import argparse
import logging
import urllib.request
import sys
from pathlib import Path
import numpy as np
import torch
import openvino as ov
import nncf

# Configure logging
logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(levelname)s - %(message)s')
logger = logging.getLogger(__name__)


def download_model_py():
    """Download official model.py from deep_sort_pytorch repository."""
    model_py_url = 'https://raw.githubusercontent.com/ZQPei/deep_sort_pytorch/master/deep_sort/deep/model.py'
    model_py_path = Path(__file__).parent / 'model.py'
    
    logger.info(f"📥 Downloading model.py from deep_sort_pytorch repository...")
    try:
        urllib.request.urlretrieve(model_py_url, model_py_path)
        logger.info(f"✅ Downloaded model.py")
    except Exception as e:
        logger.error(f"❌ Failed to download model.py: {e}")
        sys.exit(1)


# Download model.py before importing
download_model_py()

# Import official DeepSORT model architecture components
from model import BasicBlock, make_layers


class NetOriginal(torch.nn.Module):
    """
    Original Mars-Small128 Model Architecture
    
    Compatible with original_ckpt.t7 (11MB, 625 classes)
    Uses 32→64→128 channel progression with dense layers
    """
    
    def __init__(self, num_classes=625, reid=False):
        super(NetOriginal, self).__init__()
        # Smaller architecture starting with 32 channels
        self.conv = torch.nn.Sequential(
            torch.nn.Conv2d(3, 32, 3, stride=1, padding=1),  # conv.0
            torch.nn.BatchNorm2d(32),                         # conv.1
            torch.nn.ReLU(inplace=True),                      # conv.2
            torch.nn.Conv2d(32, 32, 3, stride=1, padding=1), # conv.3
            torch.nn.BatchNorm2d(32),                         # conv.4
            torch.nn.ReLU(inplace=True),                      # conv.5
            torch.nn.MaxPool2d(3, 2, padding=1),              # conv.6
        )
        # Use make_layers from model.py but with BasicBlock
        self.layer1 = make_layers(32, 32, 2, False)
        self.layer2 = make_layers(32, 64, 2, True)
        self.layer3 = make_layers(64, 128, 2, True)
        
        # Dense layers (using indices 1,2 to match checkpoint)
        self.dense = torch.nn.Sequential(
            torch.nn.Dropout(p=0),                     # dense.0 (placeholder)
            torch.nn.Linear(128 * 16 * 8, 128),        # dense.1
            torch.nn.BatchNorm1d(128),                  # dense.2
            torch.nn.ReLU(inplace=True),                # dense.3
        )
        self.batch_norm = torch.nn.BatchNorm1d(128)
        self.reid = reid
        self.classifier = torch.nn.Sequential(
            torch.nn.Linear(128, num_classes),
        )
        
    def forward(self, x):
        x = self.conv(x)
        x = self.layer1(x)
        x = self.layer2(x)
        x = self.layer3(x)
        x = x.view(x.size(0), -1)  # Flatten: [B, 128, 16, 8] -> [B, 128*16*8]
        x = self.dense(x)          # [B, 128*16*8] -> [B, 128]
        x = self.batch_norm(x)
        # Return 128-dim features in reid mode
        if self.reid:
            x = x.div(x.norm(p=2, dim=1, keepdim=True))  # L2 normalize
            return x
        # Return classifier logits
        x = self.classifier(x)
        return x


class MarsDeepSORTConverter:
    """Converter for Mars-Small128 DeepSORT models to OpenVINO IR format."""
    
    CHECKPOINT_ORG_URL = 'https://drive.google.com/uc?id=1lfCXBm5ltH-6CjJ1a5rqiZoWgGmRsZSY'  # original_ckpt.t7 (~11 MB) - Original arch (32→64→128, 625 classes)
    
    def __init__(self, output_dir: str = "./mars_deepsort_models"):
        self.output_dir = Path(output_dir)
        self.output_dir.mkdir(parents=True, exist_ok=True)
        self.input_shape = (1, 3, 128, 64)  # NCHW: batch, channels, height, width
        
    def download_checkpoint(self) -> Path:
        """Download pretrained checkpoint from Google Drive."""
        checkpoint_filename = 'original_ckpt.t7'
        checkpoint_url = self.CHECKPOINT_ORG_URL
        logger.info("📦 Downloading ORIGINAL checkpoint (11 MB, 32→64→128 channels, 625 classes)")
        
        checkpoint_path = self.output_dir / checkpoint_filename
        
        logger.info(f"📥 Downloading checkpoint from Google Drive...")
        try:
            urllib.request.urlretrieve(checkpoint_url, checkpoint_path)
        except Exception as e:
            logger.error(f"❌ Failed to download checkpoint: {e}")
            sys.exit(1)
        
        size_mb = checkpoint_path.stat().st_size / (1024 * 1024)
        logger.info(f"✅ Downloaded checkpoint: {size_mb:.2f} MB")
        
        return checkpoint_path
    
    def load_model(self, checkpoint_path: Path) -> torch.nn.Module:
        """Load model from checkpoint in reid (feature extraction) mode."""
        logger.info("🏗️  Loading DeepSORT model in reid mode...")
        logger.info("📐 Using NetOriginal architecture (32→64→128 channels, 625 classes)")
        
        model = NetOriginal(reid=True)
        
        # Load checkpoint
        checkpoint = torch.load(checkpoint_path, map_location='cpu', weights_only=False)
        
        if 'net_dict' in checkpoint:
            model.load_state_dict(checkpoint['net_dict'])
            logger.info(f"📊 Checkpoint accuracy: {checkpoint.get('acc', 'N/A')}")
            logger.info(f"🔄 Training epoch: {checkpoint.get('epoch', 'N/A')}")
        else:
            model.load_state_dict(checkpoint)
        
        model.eval()
        logger.info("✅ Model loaded successfully")
        
        return model
    
    def convert_to_fp32(self, model: torch.nn.Module, output_name: str = "mars_small128_fp32") -> Path:
        """Convert PyTorch model to OpenVINO FP32 format."""
        logger.info("🔄 Converting to OpenVINO FP32...")
        
        example_input = torch.randn(self.input_shape)
        output_path = self.output_dir / f"{output_name}.xml"
        
        # Convert to OpenVINO
        ov_model = ov.convert_model(
            model,
            example_input=example_input,
            input=[("x", self.input_shape)]
        )
        
        # Ensure static shape
        ov_model.reshape({"x": self.input_shape})
        
        # Save model
        ov.save_model(ov_model, str(output_path))
        
        logger.info(f"✅ FP32 model saved: {output_path}")
        self._verify_model(output_path)
        
        return output_path
    
    def convert_to_int8(self, model: torch.nn.Module, output_name: str = "mars_small128_int8", 
                        calibration_size: int = 200) -> Path:
        """Convert PyTorch model to OpenVINO INT8 format with NNCF quantization."""
        logger.info("🔄 Converting to OpenVINO INT8 with NNCF quantization...")
        
        example_input = torch.randn(self.input_shape)
        output_path = self.output_dir / f"{output_name}.xml"
        
        # Convert to OpenVINO FP32 first
        ov_model = ov.convert_model(
            model,
            example_input=example_input,
            input=[("x", self.input_shape)]
        )
        ov_model.reshape({"x": self.input_shape})
        
        # Generate calibration data
        logger.info(f"📊 Generating {calibration_size} calibration samples...")
        calibration_data = self._generate_calibration_data(calibration_size)
        
        def calibration_dataset():
            for data in calibration_data:
                yield data
        
        # Apply INT8 quantization
        calibration_dataset_nncf = nncf.Dataset(calibration_dataset())
        quantized_model = nncf.quantize(
            ov_model,
            calibration_dataset_nncf,
            subset_size=min(calibration_size, 100)
        )
        
        # Save quantized model
        ov.save_model(quantized_model, str(output_path))
        
        logger.info(f"✅ INT8 model saved: {output_path}")
        self._verify_model(output_path)
        
        return output_path
    
    def _generate_calibration_data(self, num_samples: int) -> list:
        """Generate synthetic person images for INT8 calibration."""
        calibration_data = []
        
        for i in range(num_samples):
            # Create synthetic person-like image
            image = np.random.randn(1, 3, 128, 64).astype(np.float32) * 0.2 + 0.5
            image = np.clip(image, 0.0, 1.0)
            calibration_data.append(image)
            
            if (i + 1) % 50 == 0:
                logger.info(f"  Generated {i + 1}/{num_samples} calibration samples")
        
        return calibration_data
    
    def _verify_model(self, model_path: Path):
        """Verify OpenVINO model can be loaded and inferred."""
        core = ov.Core()
        model = core.read_model(model_path)
        compiled = core.compile_model(model, "CPU")
        
        # Test inference
        test_input = np.random.randn(*self.input_shape).astype(np.float32)
        output = compiled([test_input])[0]
        
        logger.info(f"  📏 Input shape: {test_input.shape}")
        logger.info(f"  📏 Output shape: {output.shape}")
        logger.info(f"  📏 Output dimensions: {output.shape[-1]}")
        logger.info(f"  📏 L2 norm: {np.linalg.norm(output):.6f}")
        
        if output.shape[-1] != 128:
            logger.warning(f"⚠️  Expected 128-dim output, got {output.shape[-1]}")


def main():
    parser = argparse.ArgumentParser(
        description="Convert Mars-Small128 DeepSORT model to OpenVINO FP32 and INT8"
    )
    parser.add_argument(
        "--output-dir",
        type=str,
        default="./mars_deepsort_models",
        help="Output directory for converted models (default: ./mars_deepsort_models)"
    )
    parser.add_argument(
        "--precision",
        type=str,
        choices=["fp32", "int8", "both"],
        default="both",
        help="Precision to convert: fp32, int8, or both (default: both)"
    )
    parser.add_argument(
        "--calibration-size",
        type=int,
        default=200,
        help="Number of calibration samples for INT8 (default: 200)"
    )
    
    args = parser.parse_args()
    
    try:
        logger.info("=" * 60)
        logger.info("MARS-SMALL 128DIM DEEPSORT TO OPENVINO CONVERTER")
        logger.info("=" * 60)
        logger.info(f"Output directory: {args.output_dir}")
        logger.info(f"Target precision: {args.precision}")
        
        # Initialize converter
        converter = MarsDeepSORTConverter(args.output_dir)
        
        # Download checkpoint
        checkpoint_path = converter.download_checkpoint()
        
        # Load model
        model = converter.load_model(checkpoint_path)
        
        # Convert to requested precision(s)
        if args.precision in ["fp32", "both"]:
            converter.convert_to_fp32(model)
        
        if args.precision in ["int8", "both"]:
            converter.convert_to_int8(model, calibration_size=args.calibration_size)
        
        logger.info("=" * 60)
        logger.info("✅ CONVERSION COMPLETED SUCCESSFULLY!")
        logger.info("=" * 60)
        logger.info(f"📁 Output directory: {args.output_dir}")
        logger.info(f"📏 Input shape: (1, 3, 128, 64) - NCHW")
        logger.info(f"📏 Output: 128-dimensional L2-normalized feature vector")
        logger.info(f"🎯 Optimized for DeepSORT person re-identification")
        
        # Clean up downloaded model.py
        model_py_path = Path(__file__).parent / 'model.py'
        if model_py_path.exists():
            model_py_path.unlink()
            logger.info("🧹 Cleaned up temporary model.py file")
        
    except Exception as e:
        logger.error(f"❌ Conversion failed: {e}")
        import traceback
        traceback.print_exc()
        
        # Clean up on failure too
        model_py_path = Path(__file__).parent / 'model.py'
        if model_py_path.exists():
            model_py_path.unlink()
            logger.info("🧹 Cleaned up temporary model.py file")
        
        return 1
    
    return 0


if __name__ == "__main__":
    exit(main())
