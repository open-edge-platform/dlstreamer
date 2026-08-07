/*******************************************************************************
 * Copyright (C) 2021-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once
#include <string>

#ifdef __linux__
using FileDescriptor = int;
#elif _WIN32
// There're several ways to work with pipes in Windows:
// standard C library or WinAPI. For now, C stub is used.
#include <stdio.h>
using FileDescriptor = FILE *;
#endif

/**
 * Searches for processes which have an open file descriptor to the given file.
 *
 * @param file_name path to a file
 * @param access_mode can be "w", "r", "rw" (read or write)
 * @return count of processes which access the file with the given access mode
 */
int getOpenedByProcessesDescriptorsCount(const std::string &file_name, const std::string &access_mode);

class NamedPipe {
  public:
    enum class Mode { ReadOnly, WriteOnly };

    NamedPipe(const std::string &name, Mode mode);
    ~NamedPipe();
    NamedPipe() = delete;
    NamedPipe(const NamedPipe &) = delete;
    NamedPipe operator=(const NamedPipe &) = delete;

    int read(void *buf, std::size_t count);
    int write(const void *buf, std::size_t count);
    void close();
    std::string getName() const;

  private:
    std::string _pipeName;
    Mode _mode;
    FileDescriptor _pipeDescriptor;
};
