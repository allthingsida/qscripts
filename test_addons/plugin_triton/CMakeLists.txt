# Copyright (c) 2019-2026 Elias Bachaalany
# SPDX-License-Identifier: LicenseRef-Human-Origin-Source-1.0
#
# This file is licensed under the Human-Origin Source License v1.0.
# See LICENSE.

cmake_minimum_required(VERSION 3.27)
project(qscripts_native_triton)

# Make sure 'tritonenv.bat' script has been called and the proper environment variables are set

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Include IDA SDK bootstrap
include($ENV{IDASDK}/ida-cmake/bootstrap.cmake)
find_package(idasdk REQUIRED)

# Extract vcpkg include path from TRITON_LIB environment variable
string(REPLACE ";" ";" TRITON_LIB_LIST "$ENV{TRITON_LIB}")
foreach(LIB_PATH IN LISTS TRITON_LIB_LIST)
    # Handle both Windows backslashes and Unix forward slashes
    if(LIB_PATH MATCHES "(.+)[/\\\\]vcpkg_installed[/\\\\]([^/\\\\]+)[/\\\\]lib[/\\\\]")
        set(VCPKG_INCLUDE_DIR "${CMAKE_MATCH_1}/vcpkg_installed/${CMAKE_MATCH_2}/include")
        # Normalize path separators for consistency
        string(REPLACE "\\" "/" VCPKG_INCLUDE_DIR "${VCPKG_INCLUDE_DIR}")
        break()
    endif()
endforeach()

# Add plugin with Triton support
ida_add_plugin(qscripts_native_triton
    SOURCES
        driver.cpp
        main.cpp
    INCLUDES
        $ENV{TRITON_INCLUDES}
        ${VCPKG_INCLUDE_DIR}
    LIBRARIES
        $ENV{TRITON_LIB}
    DEBUG_ARGS
        ${CMAKE_CURRENT_LIST_DIR}/junk64.bin.i64
)

# Disable specific warnings
target_compile_options(qscripts_native_triton PRIVATE /wd4267 /wd4244)
