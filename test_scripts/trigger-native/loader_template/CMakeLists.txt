cmake_minimum_required(VERSION 3.12 FATAL_ERROR)

project(qscripts_native)

set(PLUGIN_NAME              qscripts_native)
set(PLUGIN_SOURCES           driver.cpp main.cpp idasdk.h)
set(PLUGIN_RUN_ARGS          "-t")
include($ENV{IDASDK}/ida-cmake/addons.cmake)
