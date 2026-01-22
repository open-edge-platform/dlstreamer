# ==============================================================================
# Copyright (C) 2018-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

include(FetchContent)

# Support DOWNLOAD_EXTRACT_TIMESTAMP in CMake 3.24:
if (CMAKE_VERSION VERSION_GREATER_EQUAL "3.24.0")
    cmake_policy(SET CMP0135 NEW)
endif()

add_subdirectory(itt)

if(${ENABLE_TESTS})
    if (EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/googletest/CMakeLists.txt)
        message("Using thirdparty/googletest")
        add_subdirectory(googletest)
    else()
        find_package(GTest) # sudo apt install libgtest-dev libgmock-dev
        if (GTest_FOUND AND GTest_VERSION VERSION_GREATER_EQUAL "1.10")
            message(STATUS "Found GTest: version ${GTest_VERSION}")
            include_directories(${GTEST_INCLUDE_DIRS})
        else()
            message("GTest library not found, skipping tests build")
            SET(ENABLE_TESTS OFF CACHE BOOL "Parameter to enable tests building" FORCE)
        endif()
    endif()
    
    FetchContent_Declare(
        fff
        URL https://github.com/meekrosoft/fff/archive/5111c61e1ef7848e3afd3550044a8cf4405f4199.zip
    )
    FetchContent_MakeAvailable(fff)
endif()

# Populate json dependency
FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/archive/v3.12.0.zip
)
FetchContent_MakeAvailable(nlohmann_json)
FetchContent_Declare(
    json_schema_validator
    URL https://github.com/pboettch/json-schema-validator/archive/2.4.0.tar.gz
)
FetchContent_MakeAvailable(json_schema_validator)
add_library(json-hpp ALIAS nlohmann_json)
add_library(json-schema-validator ALIAS nlohmann_json_schema_validator)

# Hint compiler about location of the header file generated.
include_directories(${CMAKE_CURRENT_BINARY_DIR})

add_subdirectory(spdlog)