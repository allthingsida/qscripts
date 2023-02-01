cmake_minimum_required(VERSION 3.2 FATAL_ERROR)

project(qscripts)

include($ENV{IDASDK}/ida-cmake/common.cmake)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

list(APPEND DISABLED_SOURCES utils_impl.cpp) # included file
set(PLUGIN_NAME              qscripts)
set(PLUGIN_SOURCES           qscripts.cpp ${DISABLED_SOURCES})

set_source_files_properties(${DISABLED_SOURCES} PROPERTIES LANGUAGE "")

generate()
