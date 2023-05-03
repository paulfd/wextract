cmake_minimum_required(VERSION 3.14)
project(WavetableExtract VERSION 0.2.0)
set (CMAKE_CXX_STANDARD 17)
set (CMAKE_EXPORT_COMPILE_COMMANDS ON)
option(WEXTRACT_DOWNLOAD_DEPENDENCIES_ONLY "Download all dependencies in the directory specified by CPM_SOURCE_CACHE" OFF)

include(cmake/dependencies.cmake)

if(NOT WEXTRACT_DOWNLOAD_DEPENDENCIES_ONLY)
    add_subdirectory(src)
endif()
