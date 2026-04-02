/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include <string>

#ifdef _WIN32
#define RPC_USE_NATIVE_WCHAR
#include <rpc.h>
#include <stdexcept>

inline std::string generate_client_id() {
    UUID uuid;
    RPC_STATUS status = UuidCreate(&uuid);
    if (status != RPC_S_OK && status != RPC_S_UUID_LOCAL_ONLY)
        throw std::runtime_error("UuidCreate failed with status " + std::to_string(status));
    RPC_WSTR szUuid = nullptr;
    status = UuidToStringW(&uuid, &szUuid);
    if (status != RPC_S_OK)
        throw std::runtime_error("UuidToStringW failed with status " + std::to_string(status));
    std::wstring wide(szUuid);
    std::string result(wide.begin(), wide.end());
    RpcStringFreeW(&szUuid);
    return result;
}

#else
#include <uuid/uuid.h>

inline std::string generate_client_id() {
    // 36 character UUID string plus terminating character
    static constexpr int kUuidStringLen = UUID_STR_LEN + 1;
    uuid_t binuuid;
    uuid_generate_random(binuuid);
    char uuid[kUuidStringLen];
    uuid_unparse(binuuid, uuid);
    return uuid;
}

#endif
