/*******************************************************************************
 * Copyright (C) 2025 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once
#include "glib.h"

class GMutexLockGuard {
  public:
    explicit GMutexLockGuard(GMutex *mutex) : _mutex(mutex) {
        g_mutex_lock(_mutex);
    }
    ~GMutexLockGuard() {
        g_mutex_unlock(_mutex);
    }

    GMutexLockGuard(const GMutexLockGuard &) = delete;
    GMutexLockGuard &operator=(const GMutexLockGuard &) = delete;

  private:
    GMutex *_mutex;
};
