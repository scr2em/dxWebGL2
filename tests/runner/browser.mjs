// SPDX-License-Identifier: GPL-3.0-or-later
//
// Copyright (C) 2026 dxWebGL2 contributors
//
// This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
// See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
//
// Shared helpers of the headless Chromium runners: finding Playwright and Chromium, and launching a
// browser that has a WebGL2 implementation without a GPU (SwiftShader through ANGLE).
//
// Environment:
//   NODE_PATH                 directory with the `playwright` package (default /opt/node22/lib/node_modules)
//   PLAYWRIGHT_BROWSERS_PATH  Playwright's browser cache (default /opt/pw-browsers)
//   CHROMIUM_PATH             explicit Chromium/Chrome executable (default: $PLAYWRIGHT_BROWSERS_PATH/chromium, a link to
//                             the executable in some images, or .../chromium/chrome; otherwise Playwright's own download)
import { createRequire } from 'node:module';
import path from 'node:path';
import fs from 'node:fs';

const require = createRequire(import.meta.url);

export function loadPlaywright() {
  try { return require('playwright'); }
  catch { /* fall through to the NODE_PATH location */ }
  const dir = process.env.NODE_PATH || '/opt/node22/lib/node_modules';
  try { return require(path.join(dir, 'playwright')); }
  catch (e) {
    console.error('The `playwright` npm package was not found (NODE_PATH=' + dir + ').\n' +
                  'Install it with `npm install -g playwright` and a browser with `npx playwright install chromium`.');
    process.exit(2);
  }
}

export function chromiumPath() {
  if (process.env.CHROMIUM_PATH) return process.env.CHROMIUM_PATH;
  const dir = process.env.PLAYWRIGHT_BROWSERS_PATH || '/opt/pw-browsers';
  const candidates = [path.join(dir, 'chromium'), path.join(dir, 'chromium', 'chrome')];
  return candidates.find((c) => fs.existsSync(c) && fs.statSync(c).isFile());
}

export async function launchBrowser() {
  const playwright = loadPlaywright();
  return playwright.chromium.launch({
    executablePath: chromiumPath(),
    args: ['--use-angle=swiftshader', '--use-gl=angle', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist',
           '--enable-webgl', '--no-sandbox', '--enable-features=SharedArrayBuffer'],
  });
}

/** Opens `url` in a new page and collects everything the page and its workers print. */
export async function openPage(browser, url, viewport = { width: 700, height: 760 }) {
  const page = await browser.newPage({ viewport });
  const lines = [];
  const state = { lines, done: false, doneMarker: 'TEST_DONE' };
  page.on('console', (m) => { lines.push(m.text()); if (m.text().includes(state.doneMarker)) state.done = true; });
  page.on('worker', (w) => w.on('console', (m) => lines.push('[worker] ' + m.text())));
  page.on('pageerror', (e) => lines.push('PAGEERROR ' + e.message));
  await page.goto(url);
  return { page, state };
}

export const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
