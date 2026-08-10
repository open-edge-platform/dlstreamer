/*******************************************************************************
 * Copyright (C) 2020-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#ifndef __FPSCOUNTER_H__
#define __FPSCOUNTER_H__

#include <gst/video/video.h>
#include <stdbool.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

void fps_counter_create_iterative(const char *intervals, bool print_std_dev, bool print_latency);
void fps_counter_create_average(unsigned int starting_frame, unsigned int interval);
void fps_counter_create_writepipe(const char *pipe_name);
void fps_counter_create_readpipe(void *el, const char *pipe_name);

void fps_counter_new_frame(GstBuffer *buffer, const char *element_name, void *gstgvael);
void fps_counter_eos(const char *element_name);
void fps_counter_set_output(FILE *out);
gboolean fps_counter_validate_intervals(const char *intervals_string);

#ifdef __cplusplus
}
#endif

#endif /* __FPSCOUNTER_H__ */
