/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include <functional>
#include <gst/gst.h>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "image.h"
#include "input_image_layer_descriptor.h"

namespace InferenceBackend {

class OutputBlob;
class Allocator;
class InputLayerDesc;
class InputImageLayerDesc;
class ImageTransformationParams;

using InferenceConfig = std::map<std::string, std::map<std::string, std::string>>;

class ImageInference {
  public:
    using Ptr = std::shared_ptr<ImageInference>;

    // Application can derive and put object instance into inference queue, see last parameter in Submit* functions

    struct IFrameBase {
      protected:
        ImageTransformationParams::Ptr image_trans_params = std::make_shared<ImageTransformationParams>();

      public:
        using Ptr = std::shared_ptr<IFrameBase>;
        virtual void SetImage(ImagePtr image) = 0;
        virtual ImagePtr GetImage() const = 0;
        virtual ImageTransformationParams::Ptr GetImageTransformationParams() {
            return image_trans_params;
        }

        virtual ~IFrameBase() = default;
    };

    typedef std::function<void(std::map<std::string, std::shared_ptr<OutputBlob>> blobs,
                               std::vector<IFrameBase::Ptr> frames)>
        CallbackFunc;
    typedef std::function<void(std::vector<IFrameBase::Ptr> frames)> ErrorHandlingFunc;

    static Ptr createImageInferenceInstance(MemoryType input_image_memory_type, const InferenceConfig &config,
                                            Allocator *allocator, CallbackFunc callback,
                                            ErrorHandlingFunc error_handler, dlstreamer::ContextPtr context);

    virtual void SubmitImage(IFrameBase::Ptr frame,
                             const std::map<std::string, std::shared_ptr<InputLayerDesc>> &input_preprocessors) = 0;

    virtual const std::string &GetModelName() const = 0;
    virtual size_t GetBatchSize() const = 0;
    virtual size_t GetNireq() const = 0;
    virtual void GetModelImageInputInfo(size_t &width, size_t &height, size_t &batch_size, int &format,
                                        int &memory_type) const = 0;

    // TODO: return map<InputLayerDesc>
    virtual std::map<std::string, std::vector<size_t>> GetModelInputsInfo() const = 0;
    // TODO: return map<OutputLayerDesc>
    virtual std::map<std::string, std::vector<size_t>> GetModelOutputsInfo() const = 0;
    virtual std::map<std::string, GstStructure *> GetModelInfoPostproc() const = 0;
    static std::map<std::string, GstStructure *>
    GetModelInfoPreproc(const std::string model_file, const gchar *pre_proc_config, const gchar *ov_extension_lib);

    virtual bool IsQueueFull() = 0;
    virtual void Flush() = 0;
    virtual void Close() = 0;

    virtual ~ImageInference() = default;
};

class Blob {
  public:
    enum class Layout { ANY = 0, NCHW = 1, NHWC = 2, NC = 193 };
    enum class Precision {
        UNSPECIFIED = 255, /**< Unspecified value. Used by default */
        MIXED = 0,         /**< Mixed value. Can be received from network. No applicable for tensors */
        FP32 = 10,         /**< 32bit floating point value */
        FP16 = 11,         /**< 16bit floating point value, 5 bit for exponent, 10 bit for mantisa */
        BF16 = 12,         /**< 16bit floating point value, 8 bit for exponent, 7 bit for mantisa*/
        FP64 = 13,         /**< 64bit floating point value */
        Q78 = 20,          /**< 16bit specific signed fixed point precision */
        I16 = 30,          /**< 16bit signed integer value */
        U4 = 39,           /**< 4bit unsigned integer value */
        U8 = 40,           /**< 8bit unsigned integer value */
        I4 = 49,           /**< 4bit signed integer value */
        I8 = 50,           /**< 8bit signed integer value */
        U16 = 60,          /**< 16bit unsigned integer value */
        I32 = 70,          /**< 32bit signed integer value */
        U32 = 74,          /**< 32bit unsigned integer value */
        I64 = 72,          /**< 64bit signed integer value */
        U64 = 73,          /**< 64bit unsigned integer value */
        BIN = 71,          /**< 1bit integer value */
        BOOL = 41,         /**< 8bit bool type */
        CUSTOM = 80        /**< custom precision has it's own name and size of elements */
    };
    virtual ~Blob() = default;
    virtual const std::vector<size_t> &GetDims() const = 0;
    size_t GetSize() const {
        const auto &dims = GetDims();
        if (dims.empty())
            return 0;

        size_t size = 1;
        for (const auto &one_dim_size : dims)
            size *= one_dim_size;

        return size;
    }
    virtual Layout GetLayout() const = 0;
    virtual Precision GetPrecision() const = 0;
};

class OutputBlob : public virtual Blob {
  public:
    using Ptr = std::shared_ptr<OutputBlob>;
    virtual const void *GetData() const = 0;
    virtual ~OutputBlob() = default;
};

class InputBlob : public virtual Blob {
  public:
    using Ptr = std::shared_ptr<InputBlob>;
    virtual void *GetData() = 0;
    virtual size_t GetIndexInBatch() const = 0;
    virtual ~InputBlob() = default;
};

// TODO: implement LayerDesc
struct InputLayerDesc {
    using Ptr = std::shared_ptr<InputLayerDesc>;
    std::string name;
    std::function<void(const InputBlob::Ptr &)> preprocessor;
    InputImageLayerDesc::Ptr input_image_preroc_params = nullptr;
};

// TODO: implement OutputLayerDesc

class Allocator {
  public:
    struct AllocContext;
    virtual void Alloc(size_t size, void *&buffer_ptr, AllocContext *&alloc_context) = 0;
    virtual void Free(AllocContext *alloc_context) = 0;
};

#define __CONFIG_KEY(name) KEY_##name
#define __DECLARE_CONFIG_KEY(name) static constexpr auto __CONFIG_KEY(name) = #name
__DECLARE_CONFIG_KEY(BASE);
__DECLARE_CONFIG_KEY(INFERENCE);
__DECLARE_CONFIG_KEY(PRE_PROCESSOR);
__DECLARE_CONFIG_KEY(INPUT_LAYER_PRECISION);
__DECLARE_CONFIG_KEY(FORMAT);
__DECLARE_CONFIG_KEY(DEVICE);
__DECLARE_CONFIG_KEY(MODEL); // Path to model
__DECLARE_CONFIG_KEY(CUSTOM_PREPROC_LIB);
__DECLARE_CONFIG_KEY(OV_EXTENSION_LIB);
__DECLARE_CONFIG_KEY(NIREQ);
__DECLARE_CONFIG_KEY(DEVICE_EXTENSIONS);
__DECLARE_CONFIG_KEY(CPU_THROUGHPUT_STREAMS); // number inference requests running in parallel
__DECLARE_CONFIG_KEY(GPU_THROUGHPUT_STREAMS);
__DECLARE_CONFIG_KEY(VPU_DEVICE_ID);
__DECLARE_CONFIG_KEY(PRE_PROCESSOR_TYPE);
__DECLARE_CONFIG_KEY(IMAGE_FORMAT);
__DECLARE_CONFIG_KEY(MODEL_FORMAT);
__DECLARE_CONFIG_KEY(BATCH_SIZE);
__DECLARE_CONFIG_KEY(RESHAPE);
__DECLARE_CONFIG_KEY(RESHAPE_STATIC);
__DECLARE_CONFIG_KEY(RESHAPE_WIDTH);
__DECLARE_CONFIG_KEY(RESHAPE_HEIGHT);
__DECLARE_CONFIG_KEY(image);
__DECLARE_CONFIG_KEY(CAPS_FEATURE);
__DECLARE_CONFIG_KEY(VAAPI_THREAD_POOL_SIZE);
__DECLARE_CONFIG_KEY(VAAPI_FAST_SCALE_LOAD_FACTOR);
__DECLARE_CONFIG_KEY(D3D11_THREAD_POOL_SIZE);
// 'mean' parameter for OpenVINO™ (gets subtracted from input values prior to division)
// 'scale' parameter for OpenVINO™ (divides pixel values)
__DECLARE_CONFIG_KEY(PIXEL_VALUE_MEAN);
__DECLARE_CONFIG_KEY(PIXEL_VALUE_SCALE);
#undef __DECLARE_CONFIG_KEY
#undef __CONFIG_KEY

} // namespace InferenceBackend
