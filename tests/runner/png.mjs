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

const crcTable = (() => {
  const t = new Uint32Array(256);
  for (let n = 0; n < 256; ++n) {
    let c = n;
    for (let k = 0; k < 8; ++k) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    t[n] = c >>> 0;
  }
  return t;
})();

function crc32(buf) {
  let c = 0xffffffff;
  for (const b of buf) c = crcTable[(c ^ b) & 255] ^ (c >>> 8);
  return (c ^ 0xffffffff) >>> 0;
}

/** Encodes RGBA8 pixels (`data`, width * height * 4 bytes) as a PNG (for difference pictures). */
export function encodePng(width, height, data) {
  const raw = Buffer.alloc((width * 4 + 1) * height);
  for (let y = 0; y < height; ++y) data.copy(raw, y * (width * 4 + 1) + 1, y * width * 4, (y + 1) * width * 4);
  const chunk = (type, body) => {
    const len = Buffer.alloc(4); len.writeUInt32BE(body.length);
    const tb = Buffer.concat([Buffer.from(type, 'latin1'), body]);
    const crc = Buffer.alloc(4); crc.writeUInt32BE(crc32(tb));
    return Buffer.concat([len, tb, crc]);
  };
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(width, 0); ihdr.writeUInt32BE(height, 4);
  ihdr[8] = 8; ihdr[9] = 6; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
  return Buffer.concat([Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]), chunk('IHDR', ihdr),
                        chunk('IDAT', zlib.deflateSync(raw)), chunk('IEND', Buffer.alloc(0))]);
}

/**
 * Compares two decoded images channel by channel (RGB): the largest and the mean absolute difference, the number of
 * pixels with a channel differing by more than `tolerance`, and a difference picture (the differing pixels in red over
 * a dimmed copy of `a`).
 */
export function compareImages(a, b, tolerance = 8) {
  if (a.width !== b.width || a.height !== b.height) throw new Error(`sizes differ: ${a.width}x${a.height} vs ${b.width}x${b.height}`);
  const n = a.width * a.height;
  let max = 0, sum = 0, over = 0;
  const diff = Buffer.alloc(n * 4);
  for (let i = 0; i < n; ++i) {
    const o = i * 4;
    let m = 0;
    for (let c = 0; c < 3; ++c) {
      const d = Math.abs(a.data[o + c] - b.data[o + c]);
      sum += d;
      if (d > m) m = d;
    }
    if (m > max) max = m;
    const bad = m > tolerance;
    if (bad) ++over;
    const g = (a.data[o] + a.data[o + 1] + a.data[o + 2]) / 12;
    diff[o] = bad ? 255 : g; diff[o + 1] = bad ? 0 : g; diff[o + 2] = bad ? 0 : g; diff[o + 3] = 255;
  }
  return { max, mean: sum / (n * 3), over, pixels: n, overShare: over / n, diff };
}
