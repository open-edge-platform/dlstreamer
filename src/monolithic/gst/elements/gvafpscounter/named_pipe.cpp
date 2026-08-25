/*******************************************************************************
 * Copyright (C) 2021-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "named_pipe.h"

#ifdef __linux__

#include <dirent.h>
#include <fcntl.h>
#include <stdexcept>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int getOpenedByProcessesDescriptorsCount(const std::string &file_name, const std::string &access_mode) {
#ifdef __linux__
    int result = 0;

    mode_t flags = S_IRUSR;
    if (access_mode == "w")
        flags = S_IWUSR;
    else if (access_mode == "rw")
        flags |= S_IWUSR;
    else if (access_mode != "r")
        throw std::runtime_error("Unexpected file mode.");

    // Iterate over /proc directory
    DIR *dir = opendir("/proc");
    if (dir == NULL)
        throw std::runtime_error("Unable to open /proc directory.");

    struct dirent *de;
    while ((de = readdir(dir))) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
            continue;
        char *endptr = nullptr;
        int pid = strtol(de->d_name, &endptr, 10);
        if (*endptr != '\0')
            continue;

        char fd_path[PATH_MAX] = {'\0'};
        std::snprintf(fd_path, sizeof(fd_path), "/proc/%d/fd", pid);

        DIR *fd_dir = opendir(fd_path);
        if (!fd_dir)
            continue;

        // Iterate over opened file descriptors and find the one we're looking for.
        struct dirent *fde;
        while ((fde = readdir(fd_dir))) {
            if (!strcmp(fde->d_name, ".") || !strcmp(fde->d_name, ".."))
                continue;

            char fde_path[PATH_MAX];
            std::snprintf(fde_path, sizeof(fde_path), "/proc/%d/fd/%s", pid, fde->d_name);
            ssize_t link_dest_size = 0;
            char link_dest[PATH_MAX];
            if ((link_dest_size = readlink(fde_path, link_dest, sizeof(link_dest) - 1)) < 0) {
                continue;
            } else {
                link_dest[link_dest_size] = '\0';
            }

            // Found our file. Lets check its permission
            if (strcmp(link_dest, file_name.c_str())) {
                continue;
            }

            struct stat st;
            if (lstat(fde_path, &st) != 0)
                continue;
            result += st.st_mode & flags;
        }
        closedir(fd_dir);
    }
    closedir(dir);
    return result;
#elif _WIN32
    assert(!"getOpenedByProcessesDescriptorsCount() is not implemented for Windows.");
    return -1;
#endif
}

namespace {

bool isDescriptorValid(const FileDescriptor &desc) {
    return desc > 0;
}

int getPipeMode(NamedPipe::Mode mode) {
    if (mode == NamedPipe::Mode::ReadOnly)
        return O_RDONLY;
    if (mode == NamedPipe::Mode::WriteOnly)
        return O_WRONLY;
    throw std::runtime_error("Invalid pipe mode.");
}
} // namespace

NamedPipe::NamedPipe(const std::string &name, NamedPipe::Mode mode) : _pipeName(name), _mode(mode) {
    // Try to create FIFO file. It's ok if it exists already
    auto ret = mkfifo(name.c_str(), 0666);
    if (ret != 0 && errno != EEXIST)
        throw std::runtime_error("Can't create pipe " + name + ": " + std::string(strerror(errno)));

    _pipeDescriptor = ::open(name.c_str(), getPipeMode(mode));
    if (!isDescriptorValid(_pipeDescriptor))
        throw std::runtime_error("Can't open pipe " + name + ".");
}

NamedPipe::~NamedPipe() {
    try {
        close();
        if (getOpenedByProcessesDescriptorsCount(_pipeName, "rw") == 0) {
            if (remove(_pipeName.c_str()))
                throw std::runtime_error("Failed to remove pipe " + _pipeName);
        }
    } catch (const std::exception &e) {
        fprintf(stderr, "Error in NamedPipe destructor: %s\n", e.what());
    } catch (...) {
        fprintf(stderr, "Unknown error in NamedPipe destructor\n");
    }
}

int NamedPipe::read(void *buf, std::size_t count) {
    return ::read(_pipeDescriptor, buf, count);
}

int NamedPipe::write(const void *buf, std::size_t count) {
    return ::write(_pipeDescriptor, buf, count);
}

void NamedPipe::close() {
    if (isDescriptorValid(_pipeDescriptor)) {
        if (::close(_pipeDescriptor) != 0)
            throw std::runtime_error("Failed to close a pipe.");
        _pipeDescriptor = -1;
    }
}

std::string NamedPipe::getName() const {
    return _pipeName;
}

#elif _WIN32
#include <cassert>

NamedPipe::NamedPipe(const std::string &, NamedPipe::Mode) {
    assert(!"NamedPipe is not implemented for Windows.");
}

NamedPipe::~NamedPipe() {
}

int NamedPipe::read(void *, std::size_t) {
    assert(!"NamedPipe::read is not implemented for Windows.");
    return -1;
}

int NamedPipe::write(const void *, std::size_t) {
    assert(!"NamedPipe::write is not implemented for Windows.");
    return -1;
}

void NamedPipe::close() {
    assert(!"NamedPipe::close is not implemented for Windows.");
}

std::string NamedPipe::getName() const {
    return _pipeName;
}
#endif
