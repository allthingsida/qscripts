cmake_minimum_required(VERSION 3.12 FATAL_ERROR)

project(qscripts_native_triton)

# Make sure 'tritonenv.bat' script has been called and the proper environment variables are set

set(PLUGIN_NAME                qscripts_native_triton)
set(PLUGIN_LINK_LIBRARIES      $ENV{TRITON_LIB})
set(PLUGIN_INCLUDE_DIRECTORIES $ENV{TRITON_INCLUDES})
set(PLUGIN_SOURCES             driver.cpp main.cpp idasdk.h)
set(PLUGIN_RUN_ARGS            ${CMAKE_CURRENT_LIST_DIR}/junk64.bin.i64)
include($ENV{IDASDK}/ida-cmake/addons.cmake)
disable_ida_warnings(qscripts_native_triton)
