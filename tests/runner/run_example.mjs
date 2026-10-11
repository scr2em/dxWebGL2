// SPDX-License-Identifier: GPL-3.0-or-later
//
// Copyright (C) 2026 dxWebGL2 contributors
//
// This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
// See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
//
// Runs an example page (examples/*) in headless Chromium for a number of frames, takes a screenshot of the
// canvas and checks pixels of it.
//
//   node run_example.mjs <build-dir> <page.html> <screenshot.png> [--frames=N] [--expect=x,y,r,g,b[,tol]]...
//                        [--expect-not=x,y,r,g,b[,tol]]... [--expect-colors=N] [--backend=webgl2|webgpu|auto]
//
//   --expect       the pixel must equal the color (per channel tolerance, default 6)
//   --expect-not   the pixel must differ from the color by more than the tolerance in some channel
//   --expect-colors the canvas must show at least N distinct colors (a rendered picture, not a flat fill)
//   --backend      the GPU API (passed to the page as -dxwebgl2-backend=...; the log must show that it is used).
//                  Without it the page gets no backend option: the library's default (AUTO) selects one
//
// Exit status: 0 all checks passed, 1 a check failed or the page reported an error, 2 timeout.
import path from 'node:path';
import fs from 'node:fs';
import { startServer } from './serve.mjs';
import { launchBrowser, openPage, sleep, takeBackend } from './browser.mjs';
import { decodePng } from './png.mjs';

const argv = process.argv.slice(2);
const backend = takeBackend(argv);
const [buildDir, pageName, shot, ...rest] = argv;
if (!buildDir || !pageName || !shot) {
  console.error('usage: node run_example.mjs <build-dir> <page.html> <screenshot.png> [--frames=N] [--expect=x,y,r,g,b[,tol]]...');
  process.exit(2);
}
let frames = 30, minColors = 0;
const expects = [], expectNots = [];
for (const a of rest) {
  if (a.startsWith('--frames=')) frames = Number(a.slice(9));
  else if (a.startsWith('--expect=')) expects.push(a.slice(9).split(',').map(Number));
  else if (a.startsWith('--expect-not=')) expectNots.push(a.slice(13).split(',').map(Number));
  else if (a.startsWith('--expect-colors=')) minColors = Number(a.slice(16));
  else { console.error('unknown argument ' + a); process.exit(2); }
}

const server = await startServer(path.resolve(buildDir));
const browser = await launchBrowser();
const pageArgs = ['--frames=' + frames];
if (backend !== 'auto') pageArgs.push('-dxwebgl2-backend=' + backend);
const url = `http://127.0.0.1:${server.address().port}/${pageName}?args=${encodeURIComponent(pageArgs.join(','))}`;
const { page, state } = await openPage(browser, url, { width: 700, height: 700 });
state.doneMarker = 'EXAMPLE_DONE';
const t0 = Date.now();
while (!state.done && Date.now() - t0 < 60000) await sleep(100);
await sleep(300);
const png = await page.locator('#canvas').screenshot();
fs.writeFileSync(shot, png);
await browser.close();
server.close();

console.log(state.lines.join('\n'));
if (!state.done) { console.log('RESULT: TIMEOUT'); process.exit(2); }

const img = decodePng(png);
let failures = state.lines.filter((l) => l.startsWith('PAGEERROR')).length;
const onWebGPU = state.lines.some((l) => l.includes('WebGPU adapter:'));
if (backend === 'webgpu' && !onWebGPU) {
  console.log('FAIL the page did not render through WebGPU');
  ++failures;
}
if (backend === 'webgl2' && (onWebGPU || !state.lines.some((l) => l.includes('WebGL2 renderer:')))) {
  console.log('FAIL the page did not render through WebGL2');
  ++failures;
}
for (const l of state.lines.filter((x) => /WebGPU (validation|internal|out of memory) error/.test(x))) { console.log('FAIL ' + l); ++failures; }
const hex = (p) => p.slice(0, 3).map((v) => v.toString(16).padStart(2, '0')).join('');
for (const [x, y, r, g, b, tol = 6] of expects) {
  const p = img.pixel(x, y);
  const ok = Math.abs(p[0] - r) <= tol && Math.abs(p[1] - g) <= tol && Math.abs(p[2] - b) <= tol;
  console.log(`${ok ? 'ok  ' : 'FAIL'} pixel (${x},${y}) = ${hex(p)}, expected ${hex([r, g, b])} +-${tol}`);
  if (!ok) ++failures;
}
for (const [x, y, r, g, b, tol = 6] of expectNots) {
  const p = img.pixel(x, y);
  const differs = Math.abs(p[0] - r) > tol || Math.abs(p[1] - g) > tol || Math.abs(p[2] - b) > tol;
  console.log(`${differs ? 'ok  ' : 'FAIL'} pixel (${x},${y}) = ${hex(p)}, must differ from ${hex([r, g, b])} by more than ${tol}`);
  if (!differs) ++failures;
}
if (minColors) {
  const colors = new Set();
  for (let i = 0; i < img.data.length; i += 4) colors.add((img.data[i] << 16) | (img.data[i + 1] << 8) | img.data[i + 2]);
  const ok = colors.size >= minColors;
  console.log(`${ok ? 'ok  ' : 'FAIL'} ${colors.size} distinct colors on the canvas (at least ${minColors} expected)`);
  if (!ok) ++failures;
}
console.log(failures ? `RESULT: FAIL (${failures})` : 'RESULT: PASS');
process.exit(failures ? 1 : 0);
