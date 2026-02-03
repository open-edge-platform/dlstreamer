/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#ifndef GVA_EXPORT_H

#ifdef _WIN32
#define DLS_EXPORT __declspec(dllexport)
#else
#define DLS_EXPORT __attribute__((visibility("default")))
#endif

#define GVA_EXPORT_H
#endif // GVA_EXPORT_H