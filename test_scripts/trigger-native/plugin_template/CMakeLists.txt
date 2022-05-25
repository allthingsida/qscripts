cmake_minimum_required(VERSION 3.2 FATAL_ERROR)

project(qscripts_native)

set(PLUGIN_NAME              qscripts_native)
set(PLUGIN_SOURCES           driver.cpp main.cpp)
set(PLUGIN_RUN_ARGS          "-t")
include($ENV{IDASDK}/ida-cmake/addons.cmake)
