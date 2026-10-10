// SPDX-License-Identifier: GPL-3.0-or-later
//
// Copyright (C) 2026 dxWebGL2 contributors
//
// This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
// See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
// Runs the scenes of d3d8_runtime_test (default, --shaders, --scene2, --scene3) and compares the hash of the
// final frame with tests/runner/reference_hashes.json, so that any change of the rendered pixels is noticed.
//
//   node run_regression.mjs <build-dir> [--update] [--backend=webgl2|webgpu]
//
// The hashes are those of headless Chromium's SwiftShader; another software renderer or GPU may differ in
// the last bit. Each backend has its own hashes (the WebGL2 ones under the scene name, the others under
// "<backend>:<scene>"): the backends rasterize edges differently, compare_backends.mjs compares their pictures.
// --update rewrites the reference file after an intended change (review the screenshots first).
import { spawnSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { takeBackend } from './browser.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
const args = process.argv.slice(2);
const backend = takeBackend(args);
const buildDir = args.find((a) => !a.startsWith('--'));
const update = args.includes('--update');
if (!buildDir) { console.error('usage: node run_regression.mjs <build-dir> [--update] [--backend=webgl2|webgpu]'); process.exit(2); }
const refFile = path.join(here, 'reference_hashes.json');
const refs = fs.existsSync(refFile) ? JSON.parse(fs.readFileSync(refFile, 'utf8')) : {};
const modes = { default: [], shaders: ['--shaders'], scene2: ['--scene2'], scene3: ['--scene3'], 'default-nos3tc': ['--no-s3tc'] };
const key = (name) => (backend === 'webgl2' ? name : `${backend}:${name}`);
const extra = backend === 'webgl2' ? [] : [`--backend=${backend}`];
let bad = 0;
for (const [name, a] of Object.entries(modes)) {
  const shot = `/tmp/dxwebgl2_${backend === 'webgl2' ? '' : backend + '_'}${name}.png`;
  const r = spawnSync('node', [path.join(here, 'run_test.mjs'), buildDir, shot, ...a, ...extra], { encoding: 'utf8' });
  const m = /FRAME_HASH ([0-9a-f]+)/.exec(r.stdout);
  const hash = m ? m[1] : null;
  if (update) { refs[key(name)] = hash; console.log(`${key(name)}: ${hash} (updated)`); continue; }
  const ok = hash && refs[key(name)] === hash;
  console.log(`${key(name)}: ${hash} ${ok ? 'OK' : 'MISMATCH (reference ' + refs[key(name)] + ')'}`);
  if (!ok) bad++;
}
if (update) fs.writeFileSync(refFile, JSON.stringify(refs, null, 2) + '\n');
console.log(bad ? `RESULT: ${bad} scene(s) differ` : 'RESULT: all scenes match');
process.exit(bad ? 1 : 0);
