// SPDX-License-Identifier: GPL-3.0-or-later
//
// Copyright (C) 2026 dxWebGL2 contributors
//
// This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
// See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
//
// A minimal PNG decoder (8 bit gray/RGB/RGBA, non-interlaced: what Chromium's screenshots are), so that the
// runners can look at pixels without a native image library.
import zlib from 'node:zlib';

export function decodePng(buf) {
  const sig = [0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a];
  for (let i = 0; i < 8; ++i) if (buf[i] !== sig[i]) throw new Error('not a PNG');
  let pos = 8, width = 0, height = 0, depth = 0, colorType = 0, interlace = 0;
  const idat = [];
  while (pos < buf.length) {
    const len = buf.readUInt32BE(pos);
    const type = buf.toString('latin1', pos + 4, pos + 8);
    const data = buf.subarray(pos + 8, pos + 8 + len);
    if (type === 'IHDR') {
      width = data.readUInt32BE(0); height = data.readUInt32BE(4);
      depth = data[8]; colorType = data[9]; interlace = data[12];
    } else if (type === 'IDAT') idat.push(data);
    else if (type === 'IEND') break;
    pos += 12 + len;
  }
  if (depth !== 8 || interlace !== 0 || ![0, 2, 6].includes(colorType)) throw new Error('unsupported PNG variant');
  const channels = colorType === 0 ? 1 : colorType === 2 ? 3 : 4;
  const raw = zlib.inflateSync(Buffer.concat(idat));
  const stride = width * channels;
  const out = Buffer.alloc(width * height * 4);
  let prev = Buffer.alloc(stride), cur = Buffer.alloc(stride);
  for (let y = 0; y < height; ++y) {
    const filter = raw[y * (stride + 1)];
    const line = raw.subarray(y * (stride + 1) + 1, (y + 1) * (stride + 1));
    for (let x = 0; x < stride; ++x) {
      const a = x >= channels ? cur[x - channels] : 0, b = prev[x], c = x >= channels ? prev[x - channels] : 0;
      let v = line[x];
      switch (filter) {
        case 1: v += a; break;
        case 2: v += b; break;
        case 3: v += (a + b) >> 1; break;
        case 4: { const p = a + b - c, pa = Math.abs(p - a), pb = Math.abs(p - b), pc = Math.abs(p - c);
                  v += (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c); break; }
        default: break;
      }
      cur[x] = v & 255;
    }
    for (let x = 0; x < width; ++x) {
      const o = (y * width + x) * 4;
      if (channels === 1) { out[o] = out[o + 1] = out[o + 2] = cur[x]; out[o + 3] = 255; }
      else { out[o] = cur[x * channels]; out[o + 1] = cur[x * channels + 1]; out[o + 2] = cur[x * channels + 2];
             out[o + 3] = channels === 4 ? cur[x * channels + 3] : 255; }
    }
    [prev, cur] = [cur, prev];
  }
  return { width, height, data: out, pixel: (x, y) => { const o = (y * width + x) * 4; return [out[o], out[o + 1], out[o + 2], out[o + 3]]; } };
}
