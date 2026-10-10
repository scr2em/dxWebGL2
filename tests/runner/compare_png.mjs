// SPDX-License-Identifier: GPL-3.0-or-later
//
// Copyright (C) 2026 dxWebGL2 contributors
//
// This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
// See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
//
// Compares two screenshots: prints the largest and the mean per-channel difference and the share of pixels that differ
// by more than the tolerance, optionally writes the difference picture.
//
//   node compare_png.mjs <a.png> <b.png> [--tolerance=8] [--diff=out.png]
import fs from 'node:fs';
import { decodePng, encodePng, compareImages } from './png.mjs';

const args = process.argv.slice(2);
const files = args.filter((a) => !a.startsWith('--'));
if (files.length !== 2) { console.error('usage: node compare_png.mjs <a.png> <b.png> [--tolerance=8] [--diff=out.png]'); process.exit(2); }
const tol = Number((args.find((a) => a.startsWith('--tolerance=')) || '--tolerance=8').slice(12));
const diffOut = (args.find((a) => a.startsWith('--diff=')) || '').slice(7);
const a = decodePng(fs.readFileSync(files[0])), b = decodePng(fs.readFileSync(files[1]));
const r = compareImages(a, b, tol);
if (diffOut) fs.writeFileSync(diffOut, encodePng(a.width, a.height, r.diff));
console.log(`max ${r.max}, mean ${r.mean.toFixed(3)}, ${r.over} of ${r.pixels} pixels (${(r.overShare * 100).toFixed(3)}%) differ by more than ${tol}`);
