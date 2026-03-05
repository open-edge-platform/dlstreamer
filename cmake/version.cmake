# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

find_package(Git QUIET)

function(dlstreamer_branch_name VAR)
    if(GIT_FOUND)
        execute_process(
                COMMAND ${GIT_EXECUTABLE} rev-parse --abbrev-ref HEAD
                WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
                OUTPUT_VARIABLE GIT_BRANCH
                RESULT_VARIABLE EXIT_CODE
                OUTPUT_STRIP_TRAILING_WHITESPACE)
        if(EXIT_CODE EQUAL 0)
            set(${VAR} ${GIT_BRANCH} PARENT_SCOPE)
        endif()
    endif()
endfunction()

function(dlstreamer_commit_hash VAR)
    if(GIT_FOUND)
        execute_process(
                COMMAND ${GIT_EXECUTABLE} rev-parse --short=11 HEAD
                WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
                OUTPUT_VARIABLE GIT_COMMIT_HASH
                RESULT_VARIABLE EXIT_CODE
                OUTPUT_STRIP_TRAILING_WHITESPACE)
        if(EXIT_CODE EQUAL 0)
            set(${VAR} ${GIT_COMMIT_HASH} PARENT_SCOPE)
        endif()
    endif()
endfunction()

function(dlstreamer_commit_number VAR)
    set(GIT_COMMIT_NUMBER_FOUND OFF)
    if(GIT_FOUND)
        execute_process(
                COMMAND ${GIT_EXECUTABLE} rev-list --count HEAD
                WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
                OUTPUT_VARIABLE GIT_COMMIT_NUMBER
                RESULT_VARIABLE EXIT_CODE
                OUTPUT_STRIP_TRAILING_WHITESPACE)
        if(EXIT_CODE EQUAL 0)
            set(GIT_COMMIT_NUMBER_FOUND ON)
            set(${VAR} ${GIT_COMMIT_NUMBER} PARENT_SCOPE)
        endif()
    endif()
    if(NOT GIT_COMMIT_NUMBER_FOUND)
        # set zeros since git is not available
        set(${VAR} "000" PARENT_SCOPE)
    endif()
endfunction()

function(dlstreamer_full_version full_version)
    if(GIT_FOUND)
        dlstreamer_branch_name(GIT_BRANCH)
        dlstreamer_commit_hash(GIT_COMMIT_HASH)
        dlstreamer_commit_number(GIT_COMMIT_NUMBER)

        if(NOT GIT_BRANCH MATCHES "^(main|HEAD)$")
            set(GIT_BRANCH_POSTFIX "-${GIT_BRANCH}")
        endif()

        set(GIT_INFO "${GIT_COMMIT_NUMBER}-${GIT_COMMIT_HASH}${GIT_BRANCH_POSTFIX}" PARENT_SCOPE)
        set(${full_version} "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}-${GIT_COMMIT_NUMBER}-${GIT_COMMIT_HASH}${GIT_BRANCH_POSTFIX}" PARENT_SCOPE)
    else()
        set(${full_version} "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}" PARENT_SCOPE)
    endif()
endfunction()

dlstreamer_full_version(PRODUCT_FULL_VERSION)
message(STATUS "Deep Learning Streamer full version: ${PRODUCT_FULL_VERSION}")
