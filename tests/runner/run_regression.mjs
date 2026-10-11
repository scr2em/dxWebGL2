// SPDX-License-Identifier: GPL-3.0-or-later
//
// Copyright (C) 2026 dxWebGL2 contributors
//
// This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
// See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
// Runs the scenes of d3d8_runtime_test (default, --shaders, --scene2, --scene3) and compares the hash of the
// final frame with tests/runner/reference_hashes.json, so that any change of the rendered pixels is noticed.
//
//   node run_regression.mjs <build-dir> [--update] [--backend=webgl2|webgpu|auto] [--expect-backend=webgl2|webgpu]
//                           [--no-webgpu-browser] [--scenes=a,b]
//
// The hashes are those of headless Chromium's SwiftShader; another software renderer or GPU may differ in
// the last bit. Each backend has its own hashes (the WebGL2 ones under the scene name, the others under
// "<backend>:<scene>"): the backends rasterize edges differently, compare_backends.mjs compares their pictures.
// --update rewrites the reference file after an intended change (review the screenshots first).
//
// --backend=auto (the default: no backend option for the test, the library's default DXWEBGL2_BACKEND_AUTO) compares
// each scene with the hashes of the backend AUTO selected; --expect-backend fails the run when AUTO selected another
// one, and in a browser without WebGPU (--no-webgpu-browser) the fallback must have logged exactly one line.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { runTest } from './run_test.mjs';
import { takeBackend, takeFlag, takeOption, unavailableLines } from './browser.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
const args = process.argv.slice(2);
const backend = takeBackend(args);
const expectBackend = takeOption(args, 'expect-backend');
const webgpuBrowser = !takeFlag(args, '--no-webgpu-browser');
const only = takeOption(args, 'scenes');
const update = takeFlag(args, '--update');
const buildDir = args.find((a) => !a.startsWith('--'));
if (!buildDir) {
  console.error('usage: node run_regression.mjs <build-dir> [--update] [--backend=webgl2|webgpu|auto] [--expect-backend=webgl2|webgpu] ' +
                '[--no-webgpu-browser] [--scenes=a,b]');
  process.exit(2);
}
if (update && backend === 'auto') { console.error('--update needs --backend=webgl2 or --backend=webgpu'); process.exit(2); }
const refFile = path.join(here, 'reference_hashes.json');
const refs = fs.existsSync(refFile) ? JSON.parse(fs.readFileSync(refFile, 'utf8')) : {};
const allModes = { default: [], shaders: ['--shaders'], scene2: ['--scene2'], scene3: ['--scene3'], 'default-nos3tc': ['--no-s3tc'] };
const modes = only ? Object.fromEntries(only.split(',').map((n) => [n, allModes[n]])) : allModes;
// The reference of a scene rendered through `b` (the WebGL2 keys are the scene names, as before there was a second backend).
const key = (b, name) => (b === 'webgl2' ? name : `${b}:${name}`);
const extra = [`--backend=${backend}`];
if (expectBackend) extra.push(`--expect-backend=${expectBackend}`);
let bad = 0;
for (const [name, a] of Object.entries(modes)) {
  if (!a) { console.log(`${name}: unknown scene`); bad++; continue; }
  const shot = `/tmp/dxwebgl2_${backend === 'webgl2' ? '' : backend + '_'}${webgpuBrowser ? '' : 'nowebgpu_'}${name}.png`;
  const r = await runTest(buildDir, shot, [...a, ...extra], { webgpuBrowser });
  const m = /FRAME_HASH ([0-9a-f]+)/.exec(r.lines.join('\n'));
  const hash = m ? m[1] : null;
  const used = backend === 'auto' ? r.backend : backend;
  const k = key(used, name);
  if (update) { refs[k] = hash; console.log(`${k}: ${hash} (updated)`); continue; }
  const problems = [];
  if (!r.pass) problems.push(`test ${r.done ? 'failed' : 'timed out'} (${r.result || 'no result'})`);
  if (backend === 'auto') {
    if (expectBackend && used !== expectBackend) problems.push(`AUTO selected ${used}, expected ${expectBackend}`);
    // A fallback to WebGL2 says why in one line, and nothing else about WebGPU.
    const why = unavailableLines(r.lines);
    if (used === 'webgl2' && !webgpuBrowser && why.length !== 1) problems.push(`${why.length} "WebGPU is not available" lines, expected 1`);
    if (used === 'webgpu' && why.length) problems.push(`WebGPU in use but logged: ${why[0]}`);
  }
  const ok = hash && refs[k] === hash && !problems.length;
  const label = backend === 'auto' ? `auto -> ${k}` : k;
  console.log(`${label}: ${hash} ${hash && refs[k] === hash ? 'OK' : 'MISMATCH (reference ' + refs[k] + ')'}${problems.length ? ' FAILED: ' + problems.join('; ') : ''}`);
  if (backend === 'auto' && !webgpuBrowser) for (const l of unavailableLines(r.lines)) console.log(`  log: ${l}`);
  if (!ok) bad++;
}
if (update) fs.writeFileSync(refFile, JSON.stringify(refs, null, 2) + '\n');
console.log(bad ? `RESULT: ${bad} scene(s) differ` : 'RESULT: all scenes match');
process.exit(bad ? 1 : 0);
