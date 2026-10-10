# SPDX-License-Identifier: GPL-3.0-or-later
#
# Copyright (C) 2026 dxWebGL2 contributors
#
# This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
# See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.

cmake_minimum_required(VERSION 3.20)

project(dxWebGL2
    VERSION 0.1.0
    DESCRIPTION "Direct3D on WebGL2 for Emscripten"
    HOMEPAGE_URL "https://github.com/scr2em/dxWebGL2"
    LANGUAGES C CXX)

# ---------------------------------------------------------------------------------------------
# Emscripten only
# ---------------------------------------------------------------------------------------------
if(NOT (EMSCRIPTEN OR CMAKE_SYSTEM_NAME STREQUAL "Emscripten"))
    message(FATAL_ERROR
        "dxWebGL2 translates Direct3D to WebGL2 and only builds with Emscripten.\n"
        "Configure with the Emscripten toolchain, for example:\n"
        "    source /path/to/emsdk/emsdk_env.sh\n"
        "    emcmake cmake -S . -B build\n"
        "or pass -DCMAKE_TOOLCHAIN_FILE=\$EMSDK/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake")
endif()

# A top-level single-configuration build defaults to an optimized one (a library of this kind is useless at -O0).
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR AND NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(DXWEBGL2_IS_TOP_LEVEL ON)
else()
    set(DXWEBGL2_IS_TOP_LEVEL OFF)
endif()

# ---------------------------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------------------------
option(DXWEBGL2_BUILD_PTHREADS
    "Also build dxwebgl2_pthreads, the variant compiled with -pthread (for applications linked with -pthread / -sPROXY_TO_PTHREAD: all objects of a pthread build need the atomics feature)"
    ${DXWEBGL2_IS_TOP_LEVEL})
option(DXWEBGL2_BUILD_TESTS "Build the tests (run them with scripts/test.sh or ctest)" ${DXWEBGL2_IS_TOP_LEVEL})
option(DXWEBGL2_BUILD_EXAMPLES "Build the examples" ${DXWEBGL2_IS_TOP_LEVEL})
option(DXWEBGL2_INSTALL "Generate the install and package config rules" ${DXWEBGL2_IS_TOP_LEVEL})
option(DXWEBGL2_USE_PROJECT_WINDOWS_H
    "The project provides its own <windows.h>/<objbase.h> (for example a Win32 compatibility layer): do not define the Win32 base types in dxwebgl2/win32_min.h. The directories of that windows.h must be reachable by every target that uses dxWebGL2: set DXWEBGL2_WINDOWS_H_TARGET to an interface target that carries them"
    OFF)
set(DXWEBGL2_WINDOWS_H_TARGET "" CACHE STRING
    "Interface target providing the project's windows.h include directories (and flags such as -fshort-wchar); only used with DXWEBGL2_USE_PROJECT_WINDOWS_H")
option(DXWEBGL2_WARNINGS_AS_ERRORS "Treat compiler warnings in the library sources as errors" OFF)
option(DXWEBGL2_WEBGPU
    "Build the WebGPU backend (docs/WEBGPU.md). The library and every application linking it then use Emscripten's emdawnwebgpu port (--use-port=emdawnwebgpu, added to the link options); the backend is selected with dxwebgl2_SetBackend / -dxwebgl2-backend=webgpu and needs an application linked with -sJSPI (or -sASYNCIFY)"
    OFF)
option(DXWEBGL2_WEBD3D8_COMPAT
    "Export the original WebD3D8_* function names (link compatibility for code of the game port that declares them itself)" ON)

if(DXWEBGL2_USE_PROJECT_WINDOWS_H AND NOT DXWEBGL2_WINDOWS_H_TARGET)
    message(WARNING "DXWEBGL2_USE_PROJECT_WINDOWS_H is ON but DXWEBGL2_WINDOWS_H_TARGET is empty: "
                    "the project's windows.h must then be found through the global include path")
endif()

# ---------------------------------------------------------------------------------------------
# Direct3D 8 API headers (freely licensed, see NOTICE.md)
#   third_party/wine/include   d3d8.h d3d8types.h d3d8caps.h   (Wine, LGPL-2.1-or-later, patched)
#   include/dx8                d3dx8*.h                         (dxWebGL2, GPL-3.0-or-later)
# ---------------------------------------------------------------------------------------------
add_library(dxwebgl2_d3d8_headers INTERFACE)
add_library(dxWebGL2::d3d8_headers ALIAS dxwebgl2_d3d8_headers)
target_include_directories(dxwebgl2_d3d8_headers SYSTEM INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/third_party/wine/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/dx8>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/dxwebgl2/dx8>)
target_include_directories(dxwebgl2_d3d8_headers INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
if(DXWEBGL2_USE_PROJECT_WINDOWS_H)
    target_compile_definitions(dxwebgl2_d3d8_headers INTERFACE DXWEBGL2_HAVE_WINDOWS_H=1)
    if(DXWEBGL2_WINDOWS_H_TARGET)
        target_link_libraries(dxwebgl2_d3d8_headers INTERFACE ${DXWEBGL2_WINDOWS_H_TARGET})
    endif()
endif()

# ---------------------------------------------------------------------------------------------
# The library
# ---------------------------------------------------------------------------------------------
# src/core: the API-independent core; src/core/gl: the WebGL2 backend; src/core/webgpu: the WebGPU backend (optional,
# DXWEBGL2_WEBGPU). See docs/ARCHITECTURE.md, docs/WEBGPU.md.
file(GLOB DXWEBGL2_CORE_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/core/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/src/core/gl/*.cpp)
if(DXWEBGL2_WEBGPU)
    file(GLOB DXWEBGL2_WEBGPU_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/core/webgpu/*.cpp)
    list(APPEND DXWEBGL2_CORE_SOURCES ${DXWEBGL2_WEBGPU_SOURCES})
endif()
file(GLOB DXWEBGL2_D3D8_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d8/*.cpp)
set(DXWEBGL2_COMPAT_SOURCES "")
if(DXWEBGL2_WEBD3D8_COMPAT)
    file(GLOB DXWEBGL2_COMPAT_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/compat/*.cpp)
endif()

# dxwebgl2_add_library(<name> <pthreads>)
function(dxwebgl2_add_library name pthreads)
    add_library(${name} STATIC ${DXWEBGL2_CORE_SOURCES} ${DXWEBGL2_D3D8_SOURCES} ${DXWEBGL2_COMPAT_SOURCES})
    add_library(dxWebGL2::${name} ALIAS ${name})

    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(${name} PUBLIC dxwebgl2_d3d8_headers)
    target_compile_features(${name} PRIVATE cxx_std_20)
    # Direct3D code stores floats in DWORDs and back (render states, shader constants), and the Direct3D
    # structures are reinterpreted across types: strict aliasing would break that.
    target_compile_options(${name} PRIVATE -fno-strict-aliasing -Wall -Wextra
        -Wno-unused-parameter -Wno-missing-field-initializers -Wno-unused-function)
    if(DXWEBGL2_WARNINGS_AS_ERRORS)
        target_compile_options(${name} PRIVATE -Werror)
    endif()

    # WebGL2 (GLES 3 headers + Emscripten's WebGL2 support library).
    target_link_options(${name} INTERFACE -sUSE_WEBGL2=1 -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2)
    if(DXWEBGL2_WEBGPU)
        # WebGPU through Emscripten's emdawnwebgpu port (<webgpu/webgpu.h>); the application links the port too.
        target_compile_definitions(${name} PRIVATE DXWEBGL2_HAVE_WEBGPU=1)
        target_compile_options(${name} PRIVATE --use-port=emdawnwebgpu)
        target_link_options(${name} INTERFACE --use-port=emdawnwebgpu)
    endif()

    if(DXWEBGL2_WEBD3D8_COMPAT)
        # A weak reference to WebD3D8_LookupProc (a Win32 layer's LoadLibrary("D3D8.DLL")) does not make the linker take
        # the object from the archive: ask for it.
        target_link_options(${name} INTERFACE "SHELL:-Wl,--undefined=WebD3D8_LookupProc")
    endif()

    if(pthreads)
        # Every object of a pthread build needs the atomics feature.
        target_compile_options(${name} PUBLIC -pthread)
        target_link_options(${name} INTERFACE -pthread)
    endif()
    set_target_properties(${name} PROPERTIES
        EXPORT_NAME ${name}
        VERSION ${PROJECT_VERSION})
endfunction()

dxwebgl2_add_library(dxwebgl2 OFF)
if(DXWEBGL2_BUILD_PTHREADS)
    dxwebgl2_add_library(dxwebgl2_pthreads ON)
endif()

# The name of the original project's library ("web_d3d8") for build trees that still refer to it.
if(DXWEBGL2_WEBD3D8_COMPAT AND NOT TARGET web_d3d8)
    add_library(web_d3d8 ALIAS dxwebgl2)
endif()

if(DXWEBGL2_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(DXWEBGL2_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# ---------------------------------------------------------------------------------------------
# Install and package config
# ---------------------------------------------------------------------------------------------
if(DXWEBGL2_INSTALL)
    set(_targets dxwebgl2 dxwebgl2_d3d8_headers)
    if(DXWEBGL2_BUILD_PTHREADS)
        list(APPEND _targets dxwebgl2_pthreads)
    endif()
    install(TARGETS ${_targets}
        EXPORT dxWebGL2Targets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

    install(DIRECTORY include/dxwebgl2 include/WebD3D8
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(DIRECTORY include/dx8/ third_party/wine/include/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/dxwebgl2/dx8
        FILES_MATCHING PATTERN "*.h" PATTERN "*.inl")
    install(FILES LICENSE NOTICE.md DESTINATION ${CMAKE_INSTALL_DOCDIR})
    install(FILES third_party/wine/COPYING.LIB DESTINATION ${CMAKE_INSTALL_DOCDIR}/third_party/wine)

    install(EXPORT dxWebGL2Targets
        NAMESPACE dxWebGL2::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/dxWebGL2)

    configure_package_config_file(cmake/dxWebGL2Config.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/dxWebGL2Config.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/dxWebGL2)
    # The library is architecture independent bytecode of one target (wasm32), no ARCH_INDEPENDENT checks.
    write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/dxWebGL2ConfigVersion.cmake
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
        ARCH_INDEPENDENT)
    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/dxWebGL2Config.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/dxWebGL2ConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/dxWebGL2)
endif()
