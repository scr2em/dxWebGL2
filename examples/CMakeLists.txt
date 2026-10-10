# SPDX-License-Identifier: GPL-3.0-or-later
#
# Copyright (C) 2026 dxWebGL2 contributors
#
# This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
# See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.

# Examples. They render on the main thread and are linked against the plain (non-pthread) library.
#
# dxwebgl2_add_example(<name> <source>): an executable <name>.html with the shared page shell.
function(dxwebgl2_add_example name source)
    add_executable(${name} ${source})
    target_compile_features(${name} PRIVATE cxx_std_17)
    target_link_libraries(${name} PRIVATE dxwebgl2)
    set_target_properties(${name} PROPERTIES SUFFIX ".html" LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shell.html)
    target_link_options(${name} PRIVATE
        -sALLOW_MEMORY_GROWTH=1 -sEXIT_RUNTIME=0 -sASSERTIONS=1
        --shell-file ${CMAKE_CURRENT_SOURCE_DIR}/shell.html)
    if(DXWEBGL2_WEBGPU)
        # The WebGPU backend (?args=-dxwebgl2-backend=webgpu) waits for the device in CreateDevice: JSPI.
        target_link_options(${name} PRIVATE -sJSPI=1)
    endif()
endfunction()

dxwebgl2_add_example(hello_triangle hello_triangle/main.cpp)
dxwebgl2_add_example(textured_cube textured_cube/main.cpp)
