// SPDX-License-Identifier: GPL-3.0-or-later
//
// Copyright (C) 2026 dxWebGL2 contributors
//
// This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
// See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
// Runs d3d8_features_test in headless Chromium (SwiftShader) for every mode of the matrix and fails when a
// check fails.
//
//   node run_features.mjs <build-dir> [--mode <name>[,<name>...]] [--show-pass] [--shots <dir>] [--backend=webgl2|webgpu|auto]
//                         [--expect-backend=webgl2|webgpu]
//
// <build-dir> is the directory that holds d3d8_features_test.html (build/em-d3d8/dxWebGL2).
// Modes (default: all):
//   basic         border, flat shading, ps.1.x texture instructions, D3DX filters, state caching, inline shader sources
//   basic-ext     the same with Chrome's draft WebGL extensions (WEBGL_draw_instanced_base_vertex_base_instance)
//   basic-nopv    the same with WEBGL_provoking_vertex switched off (triangle reordering fallback)
//   basic-nos3tc  the same with DXT decoded on the CPU
//   contextloss   WEBGL_lose_context + the application's Reset path
//   msaa          4x multisampled back buffer
//   msaa-off      multisampling requested but not available: the device falls back cleanly
//
// --backend=webgpu runs every mode on the WebGPU backend (basic-ext and basic-nopv switch WebGL details, so there they
// repeat basic); the test checks that the device really renders through the requested backend. Without --backend the
// test gets --backend=auto (the library's default selects the backend; --expect-backend names the one it must select).
//
// Needs the `playwright` npm package (NODE_PATH) and Chromium (PLAYWRIGHT_BROWSERS_PATH=/opt/pw-browsers).
import path from 'node:path';
import fs from 'node:fs';
import { startServer } from './serve.mjs';
import { loadPlaywright, chromiumPath, browserArgs, takeBackend, takeOption } from './browser.mjs';

const playwright = loadPlaywright();

const MODES = {
  'basic': ['--mode=basic'],
  'basic-ext': ['--mode=basic', '--ext'],   // browser flag below: draft WebGL extensions on (base vertex draws)
  'basic-nopv': ['--mode=basic', '--noprovoking'],
  'basic-nos3tc': ['--mode=basic', '--no-s3tc'],
  'contextloss': ['--mode=contextloss'],
  'msaa': ['--mode=msaa'],
  'msaa-off': ['--mode=msaa', '--nomsaa'],
};

const argv = process.argv.slice(2);
const backend = takeBackend(argv);
const expectBackend = takeOption(argv, 'expect-backend');
const buildDir = argv.find((a) => !a.startsWith('--'));
let wanted = Object.keys(MODES);
let showPass = false;
let shots = null;
for (let i = 0; i < argv.length; ++i) {
  if (argv[i] === '--mode') wanted = argv[++i].split(',');
  if (argv[i] === '--show-pass') showPass = true;
  if (argv[i] === '--shots') shots = argv[++i];
}
if (!buildDir) { console.error('usage: node run_features.mjs <build-dir> [--mode a,b] [--show-pass] [--shots dir]'); process.exit(2); }
if (shots) fs.mkdirSync(shots, { recursive: true });

const server = await startServer(path.resolve(buildDir));
const port = server.address().port;
const exe = chromiumPath();
const browser = await playwright.chromium.launch({ executablePath: exe, args: browserArgs() });

let failedModes = 0;
let extBrowser = null;
for (const name of wanted) {
  if (!MODES[name]) { console.error('unknown mode ' + name); failedModes++; continue; }
  let b = browser;
  if (name.endsWith('-ext')) {
    extBrowser ||= await playwright.chromium.launch({ executablePath: exe, args: browserArgs(['--enable-webgl-draft-extensions']) });
    b = extBrowser;
  }
  const page = await b.newPage({ viewport: { width: 700, height: 760 } });
  const lines = [];
  let done = false;
  page.on('console', (m) => { lines.push(m.text()); if (m.text().includes('TEST_DONE')) done = true; });
  page.on('worker', (w) => w.on('console', (m) => lines.push('[worker] ' + m.text())));
  page.on('pageerror', (e) => lines.push('PAGEERROR ' + e.message));
  const args = [...MODES[name], `--backend=${backend}`, ...(expectBackend ? [`--expect-backend=${expectBackend}`] : [])];
  await page.goto(`http://127.0.0.1:${port}/d3d8_features_test.html?args=${encodeURIComponent(args.join(','))}`);
  const t0 = Date.now();
  while (!done && Date.now() - t0 < 120000) await new Promise((r) => setTimeout(r, 200));
  await new Promise((r) => setTimeout(r, 300));
  if (shots) await page.locator('#canvas').screenshot({ path: path.join(shots, name + '.png') }).catch(() => {});
  const checks = lines.filter((l) => /^CHECK /.test(l));
  const fails = checks.filter((l) => / FAIL/.test(l));
  const known = checks.filter((l) => / XFAIL/.test(l)); // known limitations of a backend, listed but not failures
  const passes = checks.length - fails.length - known.length;
  const other = lines.filter((l) => !/^CHECK /.test(l) && !/dxWebGL2 report/.test(l));
  console.log(`== ${name}: ${passes} passed, ${fails.length} failed${known.length ? `, ${known.length} known limitation(s)` : ''}${done ? '' : ' (TIMEOUT)'}`);
  for (const l of known) console.log('   ' + l);
  if (showPass) for (const l of checks.filter((c) => / PASS/.test(c))) console.log('   ' + l);
  for (const l of fails) console.log('   ' + l);
  const noteworthy = other.filter((l) => /PAGEERROR|rror|FAILED|unsupported|shader|assemble|missing|abort|Aborted/i.test(l));
  for (const l of noteworthy.slice(0, 25)) console.log('   | ' + l.slice(0, 300));
  if (process.env.FEATURES_VERBOSE) for (const l of other) console.log('   . ' + l.slice(0, 300));
  const gpuErrors = other.filter((l) => /WebGPU (validation|internal|out of memory) error/.test(l));
  if (gpuErrors.length) console.log(`   ${gpuErrors.length} WebGPU error(s) reported`);
  if (!done || fails.length || !checks.length || gpuErrors.length) failedModes++;
  await page.close();
}
await browser.close();
if (extBrowser) await extBrowser.close();
server.close();
console.log(failedModes ? `RESULT: ${failedModes} mode(s) failed` : 'RESULT: all modes passed');
process.exit(failedModes ? 1 : 0);
