/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

// Minimal, dependency-light reader for a single 2-D float matrix stored in the
// safetensors format (https://github.com/huggingface/safetensors). Used by the
// zero-shot converter to load precomputed class embeddings without any Python
// or PyTorch runtime dependency. Only the small, well-defined subset needed for
// the embeddings artifact is supported: one F32/F16 tensor plus string metadata.
//
// Note: safetensors stores tensor data little-endian; DLStreamer targets are
// little-endian (x86_64 / aarch64-LE), so F32 data is copied verbatim.

#include <cstdint>
#include <cstring>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace post_processing {

struct SafetensorsMatrix {
    std::vector<float> data; // row-major, rows*cols
    std::size_t rows = 0;
    std::size_t cols = 0;
    std::map<std::string, std::string> metadata;
};

namespace safetensors_detail {

inline float half_to_float(uint16_t h) {
    const uint32_t sign = static_cast<uint32_t>(h & 0x8000u) << 16;
    const uint32_t exp = (h & 0x7C00u) >> 10;
    const uint32_t mant = (h & 0x03FFu);
    uint32_t f = 0;
    if (exp == 0) {
        if (mant != 0) {
            int e = 0;
            uint32_t m = mant;
            while ((m & 0x0400u) == 0) {
                e++;
                m <<= 1;
            }
            m &= 0x03FFu;
            f = sign | (static_cast<uint32_t>(127 - 15 - e) << 23) | (m << 13);
        } else {
            f = sign;
        }
    } else if (exp == 0x1F) {
        f = sign | 0x7F800000u | (mant << 13);
    } else {
        f = sign | (static_cast<uint32_t>(exp + (127 - 15)) << 23) | (mant << 13);
    }
    float out = 0.0f;
    std::memcpy(&out, &f, sizeof(out));
    return out;
}

} // namespace safetensors_detail

// Loads a 2-D float matrix from a safetensors file. The tensor is selected by the
// first matching name in preferred_names; if none match and the file holds exactly
// one tensor, that tensor is used. 1-D tensors are treated as a single row.
inline SafetensorsMatrix load_safetensors_matrix(const std::string &path,
                                                 const std::vector<std::string> &preferred_names) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open())
        throw std::runtime_error("Cannot open embeddings file: " + path);

    unsigned char lenbuf[8] = {0};
    f.read(reinterpret_cast<char *>(lenbuf), 8);
    if (f.gcount() != 8)
        throw std::runtime_error("safetensors: file too small for header length.");
    uint64_t header_len = 0;
    for (int i = 0; i < 8; ++i)
        header_len |= static_cast<uint64_t>(lenbuf[i]) << (8 * i);
    if (header_len == 0 || header_len > (1ull << 28))
        throw std::runtime_error("safetensors: implausible header length.");

    std::string header(static_cast<std::size_t>(header_len), '\0');
    f.read(&header[0], static_cast<std::streamsize>(header_len));
    if (static_cast<uint64_t>(f.gcount()) != header_len)
        throw std::runtime_error("safetensors: truncated header.");

    nlohmann::json j = nlohmann::json::parse(header);
    if (!j.is_object())
        throw std::runtime_error("safetensors: header is not a JSON object.");

    SafetensorsMatrix out;
    if (j.contains("__metadata__") && j["__metadata__"].is_object()) {
        for (auto &kv : j["__metadata__"].items())
            if (kv.value().is_string())
                out.metadata[kv.key()] = kv.value().get<std::string>();
    }

    std::string key;
    for (const auto &name : preferred_names) {
        if (j.contains(name) && j[name].is_object()) {
            key = name;
            break;
        }
    }
    if (key.empty()) {
        for (auto &kv : j.items()) {
            if (kv.key() == "__metadata__" || !kv.value().is_object())
                continue;
            if (!key.empty())
                throw std::runtime_error("safetensors: multiple tensors present; name the embeddings tensor one of "
                                         "embeddings/label_embeddings/text_embeddings.");
            key = kv.key();
        }
    }
    if (key.empty())
        throw std::runtime_error("safetensors: no embeddings tensor found.");

    const auto &t = j.at(key);
    const std::string dtype = t.at("dtype").get<std::string>();
    const auto &shape = t.at("shape");
    const auto &offs = t.at("data_offsets");
    if (!offs.is_array() || offs.size() != 2)
        throw std::runtime_error("safetensors: invalid data_offsets.");
    const uint64_t begin = offs[0].get<uint64_t>();
    const uint64_t end = offs[1].get<uint64_t>();
    if (end < begin)
        throw std::runtime_error("safetensors: data_offsets out of order.");

    std::size_t rows = 0, cols = 0;
    if (shape.is_array() && shape.size() == 1) {
        rows = 1;
        cols = shape[0].get<std::size_t>();
    } else if (shape.is_array() && shape.size() == 2) {
        rows = shape[0].get<std::size_t>();
        cols = shape[1].get<std::size_t>();
    } else {
        throw std::runtime_error("safetensors: embeddings must be 1-D or 2-D.");
    }
    if (rows == 0 || cols == 0)
        throw std::runtime_error("safetensors: zero-sized embeddings.");

    const std::size_t elem = rows * cols;
    const std::size_t dsize = static_cast<std::size_t>(end - begin);
    const std::size_t bytes_per = (dtype == "F32") ? 4u : (dtype == "F16") ? 2u : 0u;
    if (bytes_per == 0)
        throw std::runtime_error("safetensors: unsupported dtype '" + dtype + "' (expected F32 or F16).");
    if (dsize != elem * bytes_per)
        throw std::runtime_error("safetensors: tensor byte size does not match shape.");

    f.seekg(static_cast<std::streamoff>(8) + static_cast<std::streamoff>(header_len) +
                static_cast<std::streamoff>(begin),
            std::ios::beg);
    std::vector<unsigned char> raw(dsize);
    f.read(reinterpret_cast<char *>(raw.data()), static_cast<std::streamsize>(dsize));
    if (static_cast<std::size_t>(f.gcount()) != dsize)
        throw std::runtime_error("safetensors: truncated tensor data.");

    out.rows = rows;
    out.cols = cols;
    out.data.resize(elem);
    if (dtype == "F32") {
        std::memcpy(out.data.data(), raw.data(), dsize);
    } else { // F16
        const auto *h = reinterpret_cast<const uint16_t *>(raw.data());
        for (std::size_t i = 0; i < elem; ++i)
            out.data[i] = safetensors_detail::half_to_float(h[i]);
    }
    return out;
}

} // namespace post_processing
