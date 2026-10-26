/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include <gst/check/gstcheck.h>

#include <stdio.h>
#include <string.h>

#include "test_utils.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <io.h>
#include <windows.h>

#define access _access
#define F_OK 0

// Minimal opendir()/readdir()/closedir() so the directory walks below are shared with Linux
#define DT_DIR 4
#define DT_REG 8

struct dirent {
    unsigned char d_type;
    char d_name[MAX_PATH];
};

typedef struct {
    HANDLE handle;
    WIN32_FIND_DATAA data;
    BOOL has_data;
    struct dirent entry;
} DIR;

static DIR *opendir(const char *path) {
    char pattern[MAX_STR_PATH_SIZE];
    snprintf(pattern, sizeof(pattern), "%s/*", path);
    DIR *dir = g_new0(DIR, 1);
    dir->handle = FindFirstFileA(pattern, &dir->data);
    if (dir->handle == INVALID_HANDLE_VALUE) {
        g_free(dir);
        return NULL;
    }
    dir->has_data = TRUE;
    return dir;
}

static struct dirent *readdir(DIR *dir) {
    if (!dir->has_data)
        return NULL;
    g_strlcpy(dir->entry.d_name, dir->data.cFileName, sizeof(dir->entry.d_name));
    dir->entry.d_type = (dir->data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? DT_DIR : DT_REG;
    dir->has_data = FindNextFileA(dir->handle, &dir->data);
    return &dir->entry;
}

static int closedir(DIR *dir) {
    FindClose(dir->handle);
    g_free(dir);
    return 0;
}

int setenv(const char *name, const char *value, int overwrite) {
    if (!overwrite && getenv(name))
        return 0;
    return _putenv_s(name, value);
}

int usleep(unsigned int usec) {
    // Sleep() has the ~15.6 ms system timer granularity, too coarse for the timing checks in the tests
    HANDLE timer = CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (!timer) {
        Sleep(usec / 1000);
        return 0;
    }
    LARGE_INTEGER due_time;
    due_time.QuadPart = -10 * (LONGLONG)usec; // relative, in 100 ns units
    if (SetWaitableTimer(timer, &due_time, 0, NULL, NULL, FALSE))
        WaitForSingleObject(timer, INFINITE);
    CloseHandle(timer);
    return 0;
}
#else
#include <dirent.h>
#include <unistd.h>
#endif

typedef struct dirent DIRENT;

int can_dir_open(char *dir) {
    DIR *dir_handle = opendir(dir);
    if (dir_handle != NULL) {
        closedir(dir_handle);
        return TRUE;
    }
    g_print("\n\tA directory \"%s\" could not be opened\n", dir);
    return FALSE;
}

void search_file(char *search_dir, const char *file_name, char *result, const size_t result_size) {
    DIR *dir_handle = opendir(search_dir);
    if (dir_handle != NULL) {
        DIRENT *file_handle = readdir(dir_handle);
        while (file_handle != NULL) {
            if ((!file_handle->d_name) || (file_handle->d_name[0] == '.')) {
                file_handle = readdir(dir_handle);
                continue;
            }
            if (file_handle->d_type == DT_DIR) {
                char next_dir[MAX_STR_PATH_SIZE];
                snprintf(next_dir, MAX_STR_PATH_SIZE, "%s/%s/", search_dir, file_handle->d_name);
                search_file(next_dir, file_name, result, result_size);
            }
            if (file_handle->d_type == DT_REG && !strcmp(file_handle->d_name, file_name)) {
                snprintf(result, result_size, "%s/%s", search_dir, file_handle->d_name);
                break;
            }
            file_handle = readdir(dir_handle);
        }
        closedir(dir_handle);
    } else {
        g_print("\n\tA directory could not be opened\n");
    }
}

void search_model_with_precision(const char *current_path, const char *model_name, const char *precision,
                                 char *result_model_path, unsigned result_model_path_size) {
    DIR *dir;
    DIRENT *entry;

    if (!(dir = opendir(current_path)))
        return;

    while ((entry = readdir(dir)) != NULL) {
        char path[MAX_STR_PATH_SIZE];
        if (entry->d_type == DT_DIR) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
                continue;
            if ((strstr(entry->d_name, "FP") || strstr(entry->d_name, "INT")) && strcmp(entry->d_name, precision) != 0)
                continue;
            snprintf(path, sizeof(path), "%s/%s", current_path, entry->d_name);
            search_model_with_precision(path, model_name, precision, result_model_path, result_model_path_size);
        } else {
            if (strcmp(model_name, entry->d_name) == 0 && strstr(current_path, precision)) {
                snprintf(result_model_path, result_model_path_size, "%s/%s", current_path, entry->d_name);
                break;
            }
        }
    }
    closedir(dir);
}

void get_r1_model_name(char *r1_model_name, const size_t r1_model_name_size, const char *model_name,
                       const char *precision) {
    char precision_suf[16];
    if (!strcmp(precision, "FP32")) {
        g_strlcpy(precision_suf, ".xml", 16);
    } else if (!strcmp(precision, "FP16")) {
        g_strlcpy(precision_suf, "-fp16.xml", 16);
    } else if (!strcmp(precision, "INT8")) {
        g_strlcpy(precision_suf, "-int8.xml", 16);
    } else {
        ck_abort_msg("Model's precision is not correct for OpenVINO™ Toolkit R1 models");
        return;
    }
    snprintf(r1_model_name, r1_model_name_size, "%s%s", model_name, precision_suf);
    return;
}

ExitStatus file_exists(const char *file_name) {
    if (access(file_name, F_OK) != -1)
        return EXIT_STATUS_SUCCESS;
    else
        return EXIT_STATUS_FAILURE;
}

void get_env_strcat_delim(char *value, const char *env_var, const int size_of_value) {
    char *env_value = getenv(env_var);
    if (!env_value) {
        g_print("\n\t %s env variable is not set\n", env_var);
        return;
    }
    g_strlcpy(value, env_value, size_of_value);
#ifdef _WIN32
    // The paths end up in gst_parse_launch() descriptions, where '\' is an escape character
    g_strdelimit(value, "\\", '/');
#endif
    strcat(value, G_SEARCHPATH_SEPARATOR_S);
}

ExitStatus get_model_path_with_precision(char *model_path, size_t model_path_size, const char *model_name,
                                         const char *fp) {
    char models_env_dir[256];
    get_env_strcat_delim(models_env_dir, "MODELS_PATH", sizeof(models_env_dir));
    if (!models_env_dir) {
        return EXIT_STATUS_FAILURE;
    }
    // ck_assert_msg(models_env_dir != NULL, "MODELS_PATH env variable is not set");
    const char delim = G_SEARCHPATH_SEPARATOR;
    size_t str_start = 0, str_end;
    char model_env_dir[MAX_STR_PATH_SIZE];
    char model_file[256];

    g_strlcpy(model_file, model_name, 256);

    if (!(strstr(model_file, ".xml") || strstr(model_file, ".onnx"))) {
        strcat(model_file, ".xml");
    }

    for (str_end = 0; models_env_dir[str_end]; str_end++) {
        if (models_env_dir[str_end] == delim) {
            g_strlcpy(model_env_dir, models_env_dir + str_start, str_end - str_start + 1);

            if (!can_dir_open(model_env_dir)) {
                goto CONTINUE;
            }

            search_model_with_precision(model_env_dir, model_file, fp, model_path, model_path_size);
            g_print("Searching model results:\n\troot dir: %s, model file: %s,\n\tmodel path: %s\n", model_env_dir,
                    model_file, model_path);
            if (strlen(model_path) && file_exists(model_path) == EXIT_STATUS_SUCCESS) {
                return EXIT_STATUS_SUCCESS;
            }
        CONTINUE:
            str_start = str_end + 1;
        }
    }

    return EXIT_STATUS_FAILURE;
}

ExitStatus get_2019_model_path(char *model_path, size_t model_path_size, const char *model_name, const char *fp) {
    char models_dir[256];
    get_env_strcat_delim(models_dir, "MODELS_PATH", sizeof(models_dir));
    if (!models_dir) {
        return EXIT_STATUS_FAILURE;
    }
    // ck_assert_msg(models_dir != NULL, "MODELS_PATH env variable is not set");
    const char delim = G_SEARCHPATH_SEPARATOR;
    size_t str_start = 0, str_end;
    char model_dir[MAX_STR_PATH_SIZE];

    char r1_model_name[512];
    get_r1_model_name(r1_model_name, 512, model_name, fp);

    for (str_end = 0; models_dir[str_end]; str_end++) {
        if (models_dir[str_end] == delim) {
            g_strlcpy(model_dir, models_dir + str_start, str_end - str_start + 1);
            if (!can_dir_open(model_dir)) {
                goto CONTINUE;
            }

            search_file(model_dir, r1_model_name, model_path, model_path_size);
            if (file_exists(model_path) == EXIT_STATUS_SUCCESS) {
                return EXIT_STATUS_SUCCESS;
            }
        CONTINUE:
            str_start = str_end + 1;
        }
    }

    return EXIT_STATUS_FAILURE;
}

ExitStatus get_model_path(char *model_path, size_t model_path_size, const char *model_name, const char *fp) {
    ExitStatus status;
    model_path[0] = '\0';
    status = get_model_path_with_precision(model_path, model_path_size, model_name, fp);
    if (status == EXIT_STATUS_SUCCESS) {
        return status;
    }
    model_path[0] = '\0';
    status = get_2019_model_path(model_path, model_path_size, model_name, fp);
    if (status == EXIT_STATUS_FAILURE) {
        g_print("\t\tModel %s with precision %s was not found\n", model_name, fp);
    }
    return status;
}

ExitStatus get_video_file_path(char *video_file_path, size_t video_path_size, const char *filename) {
    char video_dir[256];
    get_env_strcat_delim(video_dir, "VIDEO_EXAMPLES_DIR", sizeof(video_dir));
    ck_assert_msg(video_dir != NULL, "VIDEO_EXAMPLES_DIR env variable is not set");
    if (!video_dir) {
        return EXIT_STATUS_FAILURE;
    }

    const char delim = G_SEARCHPATH_SEPARATOR;
    size_t str_start = 0, str_end;
    char current_video_dir[MAX_STR_PATH_SIZE];

    for (str_end = 0; video_dir[str_end]; str_end++) {
        if (video_dir[str_end] == delim) {
            g_strlcpy(current_video_dir, video_dir + str_start, str_end - str_start + 1);
            if (!can_dir_open(current_video_dir)) {
                goto CONTINUE;
            }

            snprintf(video_file_path, video_path_size, "%s/%s", current_video_dir, filename);
            if (file_exists(video_file_path) == EXIT_STATUS_SUCCESS)
                return EXIT_STATUS_SUCCESS;
        CONTINUE:
            str_start = str_end + 1;
        }
    }

    ck_abort_msg("Video file was not found");
    return EXIT_STATUS_FAILURE;
}

ExitStatus get_model_proc_path(char *model_proc_path, size_t model_proc_path_size, const char *filename_no_ext) {
    model_proc_path[0] = '\0';
    char model_proc_dir[256];
    get_env_strcat_delim(model_proc_dir, "MODELS_PROC_PATH", sizeof(model_proc_dir));
    if (!model_proc_dir) {
        return EXIT_STATUS_FAILURE;
    }
    // ck_assert_msg(model_proc_dir != NULL, "MODELS_PROC_PATH env variable is not set");
    const char delim = G_SEARCHPATH_SEPARATOR;
    size_t str_start = 0, str_end;
    char current_model_proc_dir[MAX_STR_PATH_SIZE];

    char model_proc_filename[256];
    g_strlcpy(model_proc_filename, filename_no_ext, 256);
    strcat(model_proc_filename, ".json");

    for (str_end = 0; model_proc_dir[str_end]; str_end++) {
        if (model_proc_dir[str_end] == delim) {
            g_strlcpy(current_model_proc_dir, model_proc_dir + str_start, str_end - str_start + 1);
            if (!can_dir_open(current_model_proc_dir)) {
                goto CONTINUE;
            }
            search_file(current_model_proc_dir, model_proc_filename, model_proc_path, model_proc_path_size);
            if (file_exists(model_proc_path) == EXIT_STATUS_SUCCESS)
                return EXIT_STATUS_SUCCESS;
        CONTINUE:
            str_start = str_end + 1;
        }
    }

    g_print("\n\tModel-proc file was not found\n");
    // ck_abort_msg("Model-proc file was not found");
    return EXIT_STATUS_FAILURE;
}
