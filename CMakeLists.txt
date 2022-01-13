cmake_minimum_required(VERSION 3.2 FATAL_ERROR)

project(qscripts)

# Included file
list(APPEND DISABLED_SOURCES utils_impl.cpp)

set(PLUGIN_NAME              qscripts)
set(PLUGIN_SOURCES           qscripts.cpp ${DISABLED_SOURCES})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include($ENV{IDASDK}/ida-cmake/addons.cmake)

set_source_files_properties(${DISABLED_SOURCES} PROPERTIES LANGUAGE "")