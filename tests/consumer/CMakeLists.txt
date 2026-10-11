# SPDX-License-Identifier: GPL-3.0-or-later
#
# Copyright (C) 2026 dxWebGL2 contributors
#
# This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
# See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.

# A minimal consumer of dxWebGL2, built the three ways a project can use it (run by run.sh; the device pages at the end
# by tests/runner/run_auto.mjs):
#   -DCONSUMER_MODE=find_package   the installed package          (-DCMAKE_PREFIX_PATH=<install prefix>)
#   -DCONSUMER_MODE=subdirectory   add_subdirectory(<source dir>) (-DDXWEBGL2_SOURCE_DIR=<repository>)
#   -DCONSUMER_MODE=fetchcontent   FetchContent with a local source override
cmake_minimum_required(VERSION 3.20)
project(dxwebgl2_consumer LANGUAGES CXX)

set(CONSUMER_MODE "find_package" CACHE STRING "find_package, subdirectory or fetchcontent")
set(DXWEBGL2_SOURCE_DIR "" CACHE PATH "repository of dxWebGL2 (subdirectory / fetchcontent modes)")

if(CONSUMER_MODE STREQUAL "find_package")
    find_package(dxWebGL2 0.1 REQUIRED)
elseif(CONSUMER_MODE STREQUAL "subdirectory")
    set(DXWEBGL2_BUILD_TESTS OFF)
    set(DXWEBGL2_BUILD_EXAMPLES OFF)
    add_subdirectory(${DXWEBGL2_SOURCE_DIR} dxwebgl2 EXCLUDE_FROM_ALL)
elseif(CONSUMER_MODE STREQUAL "fetchcontent")
    include(FetchContent)
    set(FETCHCONTENT_SOURCE_DIR_DXWEBGL2 ${DXWEBGL2_SOURCE_DIR})
    FetchContent_Declare(dxwebgl2 GIT_REPOSITORY https://github.com/scr2em/dxWebGL2.git GIT_TAG main EXCLUDE_FROM_ALL)
    set(DXWEBGL2_BUILD_TESTS OFF)
    set(DXWEBGL2_BUILD_EXAMPLES OFF)
    FetchContent_MakeAvailable(dxwebgl2)
else()
    message(FATAL_ERROR "unknown CONSUMER_MODE")
endif()

add_executable(consumer main.cpp)
target_compile_features(consumer PRIVATE cxx_std_17)
target_link_libraries(consumer PRIVATE dxWebGL2::dxwebgl2)

# The pthread variant is an option of the library; use it when present.
if(TARGET dxWebGL2::dxwebgl2_pthreads)
    add_executable(consumer_pthreads main.cpp)
    target_compile_features(consumer_pthreads PRIVATE cxx_std_17)
    target_link_libraries(consumer_pthreads PRIVATE dxWebGL2::dxwebgl2_pthreads)
endif()

# Pages that create a device with the default backend (DXWEBGL2_BACKEND_AUTO), for tests/runner/run_auto.mjs: linked with
# -sJSPI (WebGPU can be selected when the library has it) and without (the WebGPU backend cannot wait for its device:
# AUTO must select WebGL2). They print whether the library was built with the WebGPU backend.
get_target_property(_dxwebgl2_link_options dxWebGL2::dxwebgl2 INTERFACE_LINK_OPTIONS)
if("${_dxwebgl2_link_options}" MATCHES "emdawnwebgpu")
    set(_library_has_webgpu 1)
else()
    set(_library_has_webgpu 0)
endif()
foreach(_jspi 1 0)
    set(_name consumer_device)
    if(NOT _jspi)
        set(_name consumer_device_nojspi)
    endif()
    add_executable(${_name} device.cpp)
    target_compile_features(${_name} PRIVATE cxx_std_17)
    target_link_libraries(${_name} PRIVATE dxWebGL2::dxwebgl2)
    target_compile_definitions(${_name} PRIVATE CONSUMER_LIBRARY_HAS_WEBGPU=${_library_has_webgpu} CONSUMER_LINKED_WITH_JSPI=${_jspi})
    set_target_properties(${_name} PROPERTIES SUFFIX ".html")
    if(_jspi)
        target_link_options(${_name} PRIVATE -sJSPI=1)
    endif()
endforeach()
