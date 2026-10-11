// SPDX-License-Identifier: GPL-3.0-or-later
//
// Copyright (C) 2026 dxWebGL2 contributors
//
// This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
// See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
//
// Shared helpers of the headless Chromium runners: finding Playwright and Chromium, and launching a
// browser that has WebGL2 and WebGPU implementations without a GPU (SwiftShader through ANGLE, and SwiftShader's
// Vulkan for WebGPU).
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

/**
 * Chromium flags: WebGL2 from SwiftShader through ANGLE, and WebGPU from SwiftShader's Vulkan (without the Vulkan
 * flags requestDevice fails; they do not change what WebGL2 renders). `extra` adds flags. `webgpu: false` leaves the
 * WebGPU flags out: navigator.gpu still exists but requestAdapter() returns null, like a browser without WebGPU
 * (DXWEBGL2_BACKEND_AUTO must then select WebGL2).
 */
export function browserArgs(extra = [], { webgpu = true } = {}) {
  const base = ['--use-angle=swiftshader', '--use-gl=angle', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist',
                '--enable-webgl', '--no-sandbox'];
  if (!webgpu) return [...base, '--enable-features=SharedArrayBuffer', ...extra];
  return [...base, '--enable-features=SharedArrayBuffer,Vulkan', '--enable-unsafe-webgpu', '--use-vulkan=swiftshader',
          '--use-webgpu-adapter=swiftshader', ...extra];
}

export async function launchBrowser(extra = [], options = {}) {
  const playwright = loadPlaywright();
  return playwright.chromium.launch({ executablePath: chromiumPath(), args: browserArgs(extra, options) });
}

/**
 * The backend selected on a runner's command line (--backend=webgl2|webgpu|auto), removed from `args`. Without the
 * option: `fallback`, by default 'auto' = no backend option for the page, the library's default
 * (DXWEBGL2_BACKEND_AUTO: WebGPU when the browser offers it). scripts/test.sh names the backend of every run.
 */
export function takeBackend(args, fallback = 'auto') {
  const i = args.findIndex((a) => a.startsWith('--backend='));
  if (i < 0) return fallback;
  const b = args[i].slice(10);
  args.splice(i, 1);
  if (!['webgl2', 'webgpu', 'auto'].includes(b)) { console.error('unknown backend ' + b); process.exit(2); }
  return b;
}

/** Removes the flag `name` (e.g. '--no-webgpu-browser') from `args`; true when it was there. */
export function takeFlag(args, name) {
  const i = args.indexOf(name);
  if (i < 0) return false;
  args.splice(i, 1);
  return true;
}

/** The value of `--name=value` in `args` (removed from it), or `fallback`. */
export function takeOption(args, name, fallback = null) {
  const i = args.findIndex((a) => a.startsWith(`--${name}=`));
  if (i < 0) return fallback;
  const v = args[i].slice(name.length + 3);
  args.splice(i, 1);
  return v;
}

/** The log lines of the library about WebGPU being unavailable (DXWEBGL2_BACKEND_AUTO falls back with one of them). */
export const unavailableLines = (lines) => lines.filter((l) => /WebGPU (backend )?is not available/.test(l));

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
