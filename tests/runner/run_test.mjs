// SPDX-License-Identifier: GPL-3.0-or-later
//
// Copyright (C) 2026 dxWebGL2 contributors
//
// This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
// See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
//
// Runs the d3d8_runtime_test page in headless Chromium, saves a screenshot and exits with the test's verdict.
//
//   node run_test.mjs <build-dir> [screenshot.png] [test args...]
//
// e.g. node run_test.mjs build/tests /tmp/shot.png --no-s3tc
//
// Exit status: 0 the page reported "TEST_RESULT: PASS" (and, with --frames=N, kept presenting frames), 1 it reported
// a failure, 2 it timed out.
//
// Test arguments (see tests/runtime/d3d8_runtime_test.cpp): --no-s3tc (decode DXT on the CPU), --implicit
// (no explicit swap control), --shaders (vs.1.1/ps.1.x scenes), --scene2 / --scene3 (texgen, projection, lights,
// stencil, formats...), --reset, --debug (GL error checks), --frames=N (long run; two screenshots are compared
// while it renders, which shows whether frames are presented from a thread that never yields), --yield
// (emscripten_sleep(0) per frame through the OnFramePresented hook).
//
// Needs the `playwright` npm package and Chromium, see browser.mjs. WebGL2 is provided by SwiftShader.
import path from 'node:path';
import { startServer } from './serve.mjs';
import { launchBrowser, openPage, sleep } from './browser.mjs';

export async function runTest(buildDir, shot, testArgs, { page: pageName = 'd3d8_runtime_test.html', timeoutMs = 90000 } = {}) {
  const server = await startServer(path.resolve(buildDir));
  const port = server.address().port;
  const browser = await launchBrowser();
  const query = testArgs.length ? '?args=' + encodeURIComponent(testArgs.join(',')) : '';
  const { page, state } = await openPage(browser, `http://127.0.0.1:${port}/${pageName}${query}`);
  const t0 = Date.now();
  // With a running (long) test, grab two screenshots while it still renders to verify that frames are
  // presented from a thread that never yields.
  const extra = [];
  if (testArgs.some((a) => a.startsWith('--frames='))) {
    const mid = [];
    for (let i = 0; i < 2; ++i) {
      await sleep(2500);
      mid.push(await page.locator('#canvas').screenshot());
    }
    extra.push(state.done ? 'MIDRUN: test already finished' :
      `MIDRUN: screenshots during rendering ${mid[0].equals(mid[1]) ? 'IDENTICAL (frozen?)' : 'differ (animating, presenting works)'}`);
  }
  while (!state.done && Date.now() - t0 < timeoutMs) await sleep(200);
  await sleep(500);
  if (shot) await page.locator('#canvas').screenshot({ path: shot });
  const lines = state.lines.concat(extra);
  await browser.close();
  server.close();
  const result = lines.find((l) => l.includes('TEST_RESULT:')) || '';
  const frozen = lines.some((l) => l.includes('IDENTICAL (frozen?)'));
  return { lines, done: state.done, pass: /TEST_RESULT: PASS/.test(result) && !frozen, result };
}

if (import.meta.url === `file://${process.argv[1]}`) {
  const [buildDir, ...rest] = process.argv.slice(2);
  if (!buildDir) {
    console.error('usage: node run_test.mjs <build-dir> [screenshot.png] [test args...]');
    process.exit(2);
  }
  const shot = rest.length && !rest[0].startsWith('--') ? rest.shift() : 'd3d8_runtime_test.png';
  const r = await runTest(buildDir, shot, rest);
  console.log(r.lines.join('\n'));
  console.log(r.done ? (r.pass ? 'RESULT: PASS' : 'RESULT: FAIL') : 'RESULT: TIMEOUT');
  process.exit(r.done ? (r.pass ? 0 : 1) : 2);
}
