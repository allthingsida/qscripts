# Copyright (c) 2019-2026 Elias Bachaalany
# SPDX-License-Identifier: LicenseRef-Human-Origin-Source-1.0
#
# This file is licensed under the Human-Origin Source License v1.0.
# See LICENSE.

cmake_minimum_required(VERSION 3.27)
project(qscripts)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Include ida-cmake bootstrap. ida-cmake is installed alongside the SDK at
# $IDASDK/ida-cmake (see the ida-cmake README). We use it directly rather than
# the SDK's own src/cmake, which differs across SDK versions.
include($ENV{IDASDK}/ida-cmake/bootstrap.cmake)
find_package(idasdk REQUIRED)

# Resolve idacpp: optional local path (-DIDACPP_PATH) -> IDASDK -> FetchContent
set(IDACPP_PATH "" CACHE PATH "Optional path to a local idacpp checkout")
set(IDACPP_SDK_PATH   "$ENV{IDASDK}/include/idacpp")
set(IDACPP_LOCAL_PATH "${CMAKE_CURRENT_BINARY_DIR}/idacpp")

if(IDACPP_PATH AND EXISTS "${IDACPP_PATH}/CMakeLists.txt")
    message(STATUS "Using idacpp from IDACPP_PATH: ${IDACPP_PATH}")
    add_subdirectory(${IDACPP_PATH} ${CMAKE_CURRENT_BINARY_DIR}/idacpp)
elseif(EXISTS "${IDACPP_SDK_PATH}/CMakeLists.txt")
    message(STATUS "Using idacpp from IDASDK: ${IDACPP_SDK_PATH}")
    add_subdirectory(${IDACPP_SDK_PATH} ${CMAKE_CURRENT_BINARY_DIR}/idacpp)
else()
    message(STATUS "Fetching idacpp from github.com/allthingsida/libidacpp...")
    include(FetchContent)
    FetchContent_Declare(
        idacpp
        GIT_REPOSITORY https://github.com/allthingsida/libidacpp.git
        GIT_TAG main
        SOURCE_DIR ${IDACPP_LOCAL_PATH}
    )
    FetchContent_MakeAvailable(idacpp)
endif()

# Convert paths to native format
set(SAMPLE_IDB "${IDA_CMAKE_DIR}/samples/wizmo32.exe.i64")
file(TO_NATIVE_PATH "${SAMPLE_IDB}" SAMPLE_IDB)

# Disabled sources
list(APPEND DISABLED_SOURCES utils_impl.cpp)
set_source_files_properties(${DISABLED_SOURCES} PROPERTIES LANGUAGE "")

# Add plugin
ida_add_plugin(qscripts
    SOURCES
        qscripts.cpp
        ida.h
        script.hpp
        ${DISABLED_SOURCES}
    DEBUG_ARGS
        "${SAMPLE_IDB}"
)

target_link_libraries(qscripts PRIVATE idacpp::idacpp)
