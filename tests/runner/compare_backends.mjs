// SPDX-License-Identifier: GPL-3.0-or-later
//
// Copyright (C) 2026 dxWebGL2 contributors
//
// This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
// See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
//
// Renders the scenes of d3d8_runtime_test with the WebGL2 and the WebGPU backend and compares the pictures: the
// largest and the mean channel difference, and the share of pixels differing by more than the tolerance. The two
// backends rasterize some pixels differently (WebGL2 renders the image upside down, so ties at triangle edges and the
// steps of lines fall the other way; WebGPU follows Direct3D's top-left rule): a scene fails only when more than
// --max-share of its pixels differ by more than --tolerance.
//
//   node compare_backends.mjs <build-dir> [--tolerance=8] [--max-share=0.005] [--out=<dir>] [--scene=a,b]
//
// --out keeps the screenshots of both backends and a difference picture per scene (differing pixels in red).
import fs from 'node:fs';
import path from 'node:path';
import { runTest } from './run_test.mjs';
import { decodePng, encodePng, compareImages } from './png.mjs';

const args = process.argv.slice(2);
const buildDir = args.find((a) => !a.startsWith('--'));
const opt = (name, def) => { const a = args.find((x) => x.startsWith(`--${name}=`)); return a ? a.slice(name.length + 3) : def; };
if (!buildDir) { console.error('usage: node compare_backends.mjs <build-dir> [--tolerance=8] [--max-share=0.005] [--out=dir]'); process.exit(2); }
const tolerance = Number(opt('tolerance', '8'));
const maxShare = Number(opt('max-share', '0.005'));
const out = opt('out', fs.mkdtempSync('/tmp/dxwebgl2_compare_'));
fs.mkdirSync(out, { recursive: true });
const scenes = { default: [], shaders: ['--shaders'], scene2: ['--scene2'], scene3: ['--scene3'], 'default-nos3tc': ['--no-s3tc'] };
const wanted = opt('scene', Object.keys(scenes).join(',')).split(',');

let bad = 0;
for (const name of wanted) {
  const shots = {};
  let failed = false;
  for (const backend of ['webgl2', 'webgpu']) {
    shots[backend] = path.join(out, `${name}-${backend}.png`);
    const r = await runTest(buildDir, shots[backend], [...scenes[name], `--backend=${backend}`]);
    if (!r.pass) { console.log(`${name}: the ${backend} run failed (${r.result || 'no result'})`); failed = true; }
  }
  if (failed) { bad++; continue; }
  const a = decodePng(fs.readFileSync(shots.webgl2)), b = decodePng(fs.readFileSync(shots.webgpu));
  const c = compareImages(a, b, tolerance);
  fs.writeFileSync(path.join(out, `${name}-diff.png`), encodePng(a.width, a.height, c.diff));
  const ok = c.overShare <= maxShare;
  console.log(`${name}: max ${c.max}, mean ${c.mean.toFixed(3)}, ${c.over} pixels (${(c.overShare * 100).toFixed(3)}%) differ by more than ${tolerance} ${ok ? 'OK' : 'TOO DIFFERENT'}`);
  if (!ok) bad++;
}
console.log(`pictures in ${out}`);
console.log(bad ? `RESULT: ${bad} scene(s) differ between the backends` : 'RESULT: the backends agree');
process.exit(bad ? 1 : 0);
