/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copyright (C) 2026 dxWebGL2 contributors
 *
 * This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
 * See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
 */
/*
 * diagnostics. Counts every unsupported or approximated
 * Direct3D 8 feature an application actually uses (state values, formats, shader
 * opcodes, failed creations, context losses...) and prints a summary every few
 * seconds when the report is enabled (-dxwebgl2-report[=seconds] on the command
 * line, dxwebgl2_SetReport()). Off by default: every hit site is guarded by one
 * test of g_diagOn, so nothing is formatted or stored while it is off.
 */
#pragma once

#include "core/common.h"

namespace dxwebgl2 {

enum class Hit : uint8_t
{
    Unsupported,  ///< the feature is ignored; the picture may be wrong
    Approximated, ///< emulated with a known visible or numeric difference
    Failed,       ///< a create/compile/lock call failed
    Event,        ///< device level events (context lost/restored...)
    Perf,         ///< a slow path was taken (GPU read-back, CPU decode...)
};

extern bool g_diagOn;

/// Records a hit; `key` is a short stable string literal ("rs WRAP0 ...").
void DiagHit(Hit kind, const char *key);
/// printf style key (only evaluated when the report is on).
void DiagHitf(Hit kind, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

#define DXW_HIT(kind, ...) \
    do { if (::dxwebgl2::g_diagOn) ::dxwebgl2::DiagHitf(::dxwebgl2::Hit::kind, __VA_ARGS__); } while (0)

/// Per-frame Direct3D level counters (reset by Present together with g_gl).
struct D3DCounters
{
    uint32_t draws, drawsIndexed, drawsUP, primitives;
    uint32_t setRenderState, setTextureStageState, setTexture, setTransform, setShaderConstant, setStream, locks;
    uint32_t programSwitches, programsCreated, pipelineChanges, uniformUploads;
    uint32_t clears, rtSwitches, vaoCreated;
    uint32_t groupUploads[16];   ///< per UniformGroup: how often a program received it
    uint32_t textureBinds, stateApplied;
};
extern D3DCounters g_d3d;

/// Cumulative values that survive the frame boundary.
struct TotalCounters
{
    uint64_t frames, draws, glCalls, uploadBytes;
    uint32_t contextLosses, contextRestores;
};
extern TotalCounters g_totals;

/// Called by Present(): rolls the per-frame counters into the totals and the
/// interval, prints the report when its interval has elapsed.
void DiagEndFrame();
/// Prints the report now.
void DiagPrintReport(const char *reason);
/// Sets the GPU description shown in the report header.
void DiagSetGpu(const std::string &s);
/// Parses -dxwebgl2-* options from the C runtime's argument vector (once).
void ParseCommandLineOnce();

} // namespace dxwebgl2
