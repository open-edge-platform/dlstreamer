/*******************************************************************************
 * Copyright (C) 2023-2024 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define SAMPLE_AUDIO_RATE 16000
#define MULTIPLIER 62500 // NANO_SECOND/SAMPLE_AUDIO_RATE
#define AUDIO_CAPS "audio/x-raw,format=S16LE,rate=16000,channels=1,layout=interleaved"
#define MAX_PROC_FILE_SIZE 4194304     // In bytes, 4MB
#define MAX_MODEL_FILE_SIZE 1000000000 // In bytes, 1 GB
#define FQ_PARAMS_MIN -13.1484375
#define FQ_PARAMS_MAX 13.0390625
#define FQ_PARAMS_SCALE 26.1875 // FQ_PARAMS_MAX - FQ_PARAMS_MIN;

#ifdef __cplusplus
}

#endif
