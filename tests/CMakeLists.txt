# SPDX-License-Identifier: GPL-3.0-or-later
#
# Copyright (C) 2026 dxWebGL2 contributors
#
# This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
# See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.

# Tests. Everything here is built for Emscripten; the node-only tests run through CMAKE_CROSSCOMPILING_EMULATOR
# (set by emcmake), the browser tests through tests/runner/run_test.mjs (see scripts/test.sh).

# --- Browser test: renders scenes through the D3D8 API in headless Chromium ---------------------------
if(TARGET dxwebgl2_pthreads)
    add_executable(d3d8_runtime_test runtime/d3d8_runtime_test.cpp)
    target_compile_features(d3d8_runtime_test PRIVATE cxx_std_20)
    target_link_libraries(d3d8_runtime_test PRIVATE dxwebgl2_pthreads)
    set_target_properties(d3d8_runtime_test PROPERTIES SUFFIX ".html")
    target_link_options(d3d8_runtime_test PRIVATE
        -sPROXY_TO_PTHREAD=1 -sOFFSCREENCANVAS_SUPPORT=1 -sJSPI=1 -sALLOW_MEMORY_GROWTH=1
        -sINITIAL_MEMORY=64MB -sEXIT_RUNTIME=0 -sASSERTIONS=1
        --shell-file ${CMAKE_CURRENT_SOURCE_DIR}/runtime/shell.html)
    set_target_properties(d3d8_runtime_test PROPERTIES LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/runtime/shell.html)

    # Feature tests: checked scenes for border colours, provoking vertex, the ps.1.x texture instructions, context loss,
    # multisampling, D3DX filters, state caching, diagnostics and inline shader sources (tests/runner/run_features.mjs).
    add_executable(d3d8_features_test runtime/d3d8_features_test.cpp)
    target_compile_features(d3d8_features_test PRIVATE cxx_std_20)
    target_link_libraries(d3d8_features_test PRIVATE dxwebgl2_pthreads)
    set_target_properties(d3d8_features_test PROPERTIES SUFFIX ".html")
    target_link_options(d3d8_features_test PRIVATE
        -sPROXY_TO_PTHREAD=1 -sOFFSCREENCANVAS_SUPPORT=1 -sJSPI=1 -sALLOW_MEMORY_GROWTH=1
        -sINITIAL_MEMORY=64MB -sEXIT_RUNTIME=0 -sASSERTIONS=1
        --shell-file ${CMAKE_CURRENT_SOURCE_DIR}/runtime/shell.html)
    set_target_properties(d3d8_features_test PROPERTIES LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/runtime/shell.html)
endif()

# --- Unit tests, run in node ------------------------------------------------------------------------
# ABI of the Direct3D 8 headers (compile time assertions; running it only confirms the build).
add_executable(test_header_layout unit/test_header_layout.cpp)
target_compile_features(test_header_layout PRIVATE cxx_std_20)
target_include_directories(test_header_layout PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_header_layout PRIVATE dxwebgl2_d3d8_headers)
add_test(NAME header_layout COMMAND test_header_layout)

# D3DX8 math and matrix stack (only the objects of the library that the test references are linked).
add_executable(test_d3dx_math unit/test_d3dx_math.cpp)
target_compile_features(test_d3dx_math PRIVATE cxx_std_20)
target_link_libraries(test_d3dx_math PRIVATE dxwebgl2)
add_test(NAME d3dx_math COMMAND test_d3dx_math)
set_tests_properties(header_layout d3dx_math PROPERTIES FAIL_REGULAR_EXPRESSION "FAILED")

# Wide strings do not depend on the size of wchar_t: the same test with a 32-bit and a 16-bit wchar_t.
add_executable(test_wide_strings unit/test_wide_strings.cpp)
target_compile_features(test_wide_strings PRIVATE cxx_std_20)
target_link_libraries(test_wide_strings PRIVATE dxwebgl2)
add_test(NAME wide_strings COMMAND test_wide_strings)
add_executable(test_wide_strings_short_wchar unit/test_wide_strings.cpp)
target_compile_features(test_wide_strings_short_wchar PRIVATE cxx_std_20)
target_compile_options(test_wide_strings_short_wchar PRIVATE -fshort-wchar)
target_link_libraries(test_wide_strings_short_wchar PRIVATE dxwebgl2)
add_test(NAME wide_strings_short_wchar COMMAND test_wide_strings_short_wchar)
set_tests_properties(wide_strings wide_strings_short_wchar PROPERTIES FAIL_REGULAR_EXPRESSION "FAILED")

# A project with its own <windows.h>: nothing of dxwebgl2/win32_min.h may be defined, the ABI must be the same.
# Built with a 16-bit wchar_t like such projects use, against the library that was built without.
foreach(variant detected explicit)
    add_executable(test_project_windows_h_${variant} integration/test_project_windows_h.cpp)
    target_compile_features(test_project_windows_h_${variant} PRIVATE cxx_std_20)
    target_compile_options(test_project_windows_h_${variant} PRIVATE -fshort-wchar)
    target_include_directories(test_project_windows_h_${variant} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/integration/project_windows ${CMAKE_CURRENT_SOURCE_DIR})
    if(variant STREQUAL "explicit")
        # A windows.h that dxWebGL2 cannot recognize: the project says so with the define.
        target_compile_definitions(test_project_windows_h_${variant} PRIVATE TEST_DEFINE_FLAG=1 TEST_NO_WINDOWS_GUARD=1)
    endif()
    # Only the headers of the Direct3D API (not their Windows base) are taken from dxWebGL2's targets.
    target_link_libraries(test_project_windows_h_${variant} PRIVATE dxwebgl2)
    add_test(NAME project_windows_h_${variant} COMMAND test_project_windows_h_${variant})
    set_tests_properties(project_windows_h_${variant} PROPERTIES FAIL_REGULAR_EXPRESSION "FAIL")
endforeach()

# --- Repository checks ------------------------------------------------------------------------------
find_program(DXWEBGL2_PYTHON NAMES python3 python)
if(DXWEBGL2_PYTHON)
    set(_root ${PROJECT_SOURCE_DIR})
    add_test(NAME check_spdx COMMAND ${DXWEBGL2_PYTHON} ${_root}/tools/check_spdx.py)
    add_test(NAME check_layering COMMAND ${DXWEBGL2_PYTHON} ${_root}/tools/check_layering.py)
    add_test(NAME check_interfaces_golden
        COMMAND ${DXWEBGL2_PYTHON} ${_root}/tools/check_header_compat.py --check-golden ${CMAKE_CURRENT_SOURCE_DIR}/data/d3d8_interfaces.golden)
    add_test(NAME check_webd3d8_compat COMMAND ${DXWEBGL2_PYTHON} ${_root}/tools/gen_webd3d8_compat.py --check)
    add_test(NAME check_sync_rules COMMAND ${DXWEBGL2_PYTHON} ${_root}/tools/sync/sync_from_game.py --self-check)
endif()

# Compatibility with the original WebD3D8 library: old header (source) and old symbol names (link).
add_executable(test_webd3d8_header unit/test_webd3d8_header.cpp)
target_compile_features(test_webd3d8_header PRIVATE cxx_std_20)
target_link_libraries(test_webd3d8_header PRIVATE dxwebgl2)
add_test(NAME webd3d8_header COMMAND test_webd3d8_header)
add_executable(test_webd3d8_symbols unit/test_webd3d8_symbols.cpp)
target_compile_features(test_webd3d8_symbols PRIVATE cxx_std_20)
target_link_libraries(test_webd3d8_symbols PRIVATE dxwebgl2)
add_test(NAME webd3d8_symbols COMMAND test_webd3d8_symbols)
set_tests_properties(webd3d8_header webd3d8_symbols PROPERTIES FAIL_REGULAR_EXPRESSION "FAIL")
