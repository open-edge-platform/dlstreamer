/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "dlstreamer/vaapi/context.h"
#include "inference_backend/image.h"
#include "inference_backend/logger.h"
#include "so_loader.h"

#include <va/va_backend.h>

#include <memory>
#include <stdexcept>
#include <string>

namespace internal {

/**
 * Class handles libva shared objects.
 * Has private methods to get/invoke functions from shared objects.
 * And has a public methods to call specific functions from specific shared objects.
 */
class VaApiLibBinderImpl {
  public:
    VaApiLibBinderImpl();
    ~VaApiLibBinderImpl();
    VaApiLibBinderImpl(const VaApiLibBinderImpl &) = delete;
    VaApiLibBinderImpl(VaApiLibBinderImpl &&) = delete;
    VaApiLibBinderImpl &operator=(const VaApiLibBinderImpl &) = delete;
    VaApiLibBinderImpl &operator=(VaApiLibBinderImpl &&) = delete;

    VADisplay GetDisplayDRM(int file_descriptor);
    VAStatus Initialize(VADisplay dpy, int *major_version, int *minor_version);
    VAStatus Terminate(VADisplay dpy);
    std::function<const char *(VAStatus)> StatusToStrFunc();

  private:
    /**
     * Loaded libva & libva-drm shared objects.
     * Needed to call 'vaGetDisplayDRM' and 'vaInitialize'.
     * Further to invoke libva methods VADriverContext->vtable is used.
     */
    SharedObject::Ptr _libva_so;
    SharedObject::Ptr _libva_drm_so;
};

} /* namespace internal */

/**
 * Wrapper around VaDisplay.
 * Needed for more convenient usage of VADisplay and its fields.
 */
class VaDpyWrapper final {
  public:
    explicit VaDpyWrapper() = default;
    explicit VaDpyWrapper(VADisplay d) : _dpy(d) {
        if (!isDisplayValid(_dpy))
            throw std::invalid_argument("VADisplay is invalid.");
    }

    static VaDpyWrapper fromHandle(VADisplay d) {
        return VaDpyWrapper(d);
    }

    static bool isDisplayValid(VADisplay d) noexcept {
        auto pDisplayContext = reinterpret_cast<VADisplayContextP>(d);
        return d && pDisplayContext && (pDisplayContext->vadpy_magic == VA_DISPLAY_MAGIC) &&
               pDisplayContext->pDriverContext;
    }

    VADisplay raw() const noexcept {
        return _dpy;
    }

    explicit operator bool() const noexcept {
        return isDisplayValid(_dpy);
    }

    VADisplayContextP dpyCtx() const noexcept {
        return reinterpret_cast<VADisplayContextP>(_dpy);
    }

    VADriverContextP drvCtx() const noexcept {
        return dpyCtx()->pDriverContext;
    }

    const VADriverVTable &drvVtable() const noexcept {
        return *drvCtx()->vtable;
    }

    int currentSubDevice() const;

  private:
    VADisplay _dpy = nullptr;
};

/**
 * @brief Creates a context object using specified device.
 * Internally creates and initializes VADisplay.
 *
 * @param relative_device_index - relative DRI render device index
 * @return dlstreamer::VAAPIContextPtr - created context object
 */
dlstreamer::VAAPIContextPtr vaApiCreateVaDisplay(uint32_t relative_device_index = 0);

/**
 * Singleton class to handle VaApiLibBinderImpl.
 * Multiple instances of VaApiLibBinderImpl is not needed.
 * Static modifier is used to simplify the usage and avoid workarounds.
 */
class VaApiLibBinder {
  public:
    /* access method */
    static internal::VaApiLibBinderImpl &get();

  private:
    /* private constructor to prevent instantiation */
    VaApiLibBinder();
};

/* function loaded from libva to represent VAStatus as string */
static const std::function<const char *(VAStatus)> status_to_string = VaApiLibBinder::get().StatusToStrFunc();

#define VA_CALL(_FUNC)                                                                                                 \
    {                                                                                                                  \
        ITT_TASK(#_FUNC);                                                                                              \
        VAStatus _status = _FUNC;                                                                                      \
        if (_status != VA_STATUS_SUCCESS) {                                                                            \
            throw std::runtime_error(#_FUNC " failed, sts=" + std::to_string(_status) + " " +                          \
                                     status_to_string(_status));                                                       \
        }                                                                                                              \
    }
