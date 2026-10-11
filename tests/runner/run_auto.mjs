// SPDX-License-Identifier: GPL-3.0-or-later
//
// Copyright (C) 2026 dxWebGL2 contributors
//
// This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
// See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
//
// Checks which backend the default, DXWEBGL2_BACKEND_AUTO, selects: the consumer device pages (tests/consumer/device.cpp,
// built by tests/consumer/run.sh against the installed package, with and without -sJSPI, and against the sources with
// add_subdirectory) are opened in Chromium with WebGPU, without its WebGPU flags (no adapter), with navigator.gpu
// removed and with a requestDevice() that fails. AUTO must select WebGPU exactly when the library has the backend,
// the page is linked with JSPI and the browser has an adapter and a device; otherwise WebGL2, with one log line saying
// why when the library has the WebGPU backend and none when it has not. The canvas must show the cleared colour.
//
//   node run_auto.mjs <consumers-dir>        (scripts/test.sh: build/consumers)
import path from 'node:path';
import fs from 'node:fs';
import { startServer } from './serve.mjs';
import { launchBrowser, openPage, sleep, unavailableLines } from './browser.mjs';
import { decodePng } from './png.mjs';

const dir = process.argv[2];
if (!dir) { console.error('usage: node run_auto.mjs <consumers-dir>'); process.exit(2); }

// Ways the browser can lack WebGPU, applied before the page's scripts run.
const BROWSERS = {
  'WebGPU': { webgpu: true },
  'no adapter': { webgpu: false },
  'no navigator.gpu': { webgpu: true, init: () => { delete Navigator.prototype.gpu; } },
  'requestDevice fails': {
    webgpu: true,
    init: () => { GPUAdapter.prototype.requestDevice = () => Promise.reject(new Error('rejected by the test')); },
  },
};
// Pages and the browsers they are opened in.
const PAGES = [
  { page: 'find_package/consumer_device.html', browsers: Object.keys(BROWSERS) },
  { page: 'find_package/consumer_device_nojspi.html', browsers: ['WebGPU'] },
  { page: 'subdirectory/consumer_device.html', browsers: ['WebGPU'] },
];

const server = await startServer(path.resolve(dir));
const port = server.address().port;
const browsers = {};
let failures = 0;
for (const { page: pageName, browsers: names } of PAGES) {
  if (!fs.existsSync(path.join(dir, pageName))) { console.log(`FAIL ${pageName}: not built`); ++failures; continue; }
  for (const name of names) {
    const b = BROWSERS[name];
    browsers[b.webgpu] ||= await launchBrowser([], { webgpu: b.webgpu });
    const context = await browsers[b.webgpu].newContext();
    if (b.init) await context.addInitScript(b.init);
    const p = await context.newPage();
    const lines = [];
    let done = false;
    p.on('console', (m) => { lines.push(m.text()); if (m.text().includes('CONSUMER_DEVICE_DONE')) done = true; });
    p.on('pageerror', (e) => lines.push('PAGEERROR ' + e.message));
    await p.goto(`http://127.0.0.1:${port}/${pageName}`);
    const t0 = Date.now();
    while (!done && Date.now() - t0 < 30000) await sleep(100);
    await sleep(300);
    const png = done ? await p.locator('#canvas').screenshot() : null;
    await context.close();

    const build = /BUILD webgpu=(\d) jspi=(\d)/.exec(lines.join('\n'));
    const used = (/BACKEND (webgl2|webgpu)/.exec(lines.join('\n')) || [])[1];
    const libraryHasWebGPU = build && build[1] === '1', jspi = build && build[2] === '1';
    const browserHasWebGPU = name === 'WebGPU';
    const expected = libraryHasWebGPU && jspi && browserHasWebGPU ? 'webgpu' : 'webgl2';
    const why = unavailableLines(lines);
    const problems = [];
    if (!done) problems.push('timeout');
    if (!build) problems.push('no BUILD line');
    if (used !== expected) problems.push(`selected ${used || 'nothing'}, expected ${expected}`);
    const wantLines = expected === 'webgl2' && libraryHasWebGPU ? 1 : 0;
    if (why.length !== wantLines) problems.push(`${why.length} "WebGPU is not available" line(s), expected ${wantLines}`);
    for (const l of lines.filter((x) => /PAGEERROR|Aborted|WebGPU (validation|internal|out of memory) error|device was lost/.test(x))) problems.push(l);
    if (png) {
      const img = decodePng(png);
      const [r, g, bl] = img.pixel(Math.floor(img.width / 2), Math.floor(img.height / 2));
      if (Math.abs(r - 32) > 2 || Math.abs(g - 96) > 2 || Math.abs(bl - 160) > 2) problems.push(`canvas shows ${r},${g},${bl}, expected 32,96,160`);
    }
    const reason = why.length ? ` (${why[0].replace(/^.*?using WebGL2: /, '')})` : '';
    console.log(`${problems.length ? 'FAIL' : 'ok  '} ${pageName}, browser: ${name}: ${used || '?'}${reason}${problems.length ? ': ' + problems.join('; ') : ''}`);
    if (problems.length) { ++failures; for (const l of lines.slice(0, 30)) console.log('     | ' + l); }
  }
}
for (const b of Object.values(browsers)) await b.close();
server.close();
console.log(failures ? `RESULT: ${failures} check(s) failed` : 'RESULT: AUTO selects the expected backend everywhere');
process.exit(failures ? 1 : 0);
