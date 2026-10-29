# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set(TARGET_NAME "dls_devices")

find_package(OpenVINO REQUIRED Runtime)

add_executable(${TARGET_NAME} dls_devices.cpp)
set_compile_flags(${TARGET_NAME})
if(WIN32)
    add_vs_version_resource(${TARGET_NAME})
endif()

target_link_libraries(${TARGET_NAME}
PRIVATE
    openvino::runtime
    nlohmann_json::nlohmann_json
)

install(TARGETS ${TARGET_NAME} RUNTIME DESTINATION bin COMPONENT c01_runtime)
