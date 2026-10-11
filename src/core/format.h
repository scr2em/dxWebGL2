/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copyright (C) 2026 dxWebGL2 contributors
 *
 * This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
 * See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
 */
/*
 * D3DFORMAT descriptions, pixel format conversion, and the storage the WebGL2 backend uses for each format (the
 * gl* columns; GL enum values, read by src/core/gl only).
 */
#pragma once

#include "core/common.h"

namespace dxwebgl2 {

/// How the memory layout of a D3D format is turned into what the WebGL2 backend uploads.
enum class UploadConv : uint8_t
{
    None,          ///< Bytes are uploaded unchanged.
    BGRA8,         ///< A8R8G8B8 -> RGBA8
    BGRX8,         ///< X8R8G8B8 -> RGBA8 with alpha = 255
    BGR8,          ///< R8G8B8 -> RGB8
    A1R5G5B5,      ///< -> RGB5_A1
    X1R5G5B5,      ///< -> RGB5_A1 with alpha = 1
    A4R4G4B4,      ///< -> RGBA4
    X4R4G4B4,      ///< -> RGBA4 with alpha = 15
    A4L4,          ///< -> LUMINANCE_ALPHA 8/8
    DXT,           ///< Block compressed, uploaded directly or decoded to RGBA8
};

struct FormatInfo
{
    D3DFORMAT d3d;
    const char *name;
    uint8_t bits;        ///< bits per pixel; 0 for block compressed formats
    uint8_t blockBytes;  ///< bytes per 4x4 block for block compressed formats
    bool hasAlpha;
    bool texture;        ///< can be created as a texture
    bool renderTarget;   ///< can be used as a render target
    bool depth;          ///< depth/stencil format
    bool stencil;
    UploadConv conv;
    // WebGL2 storage (GL enum values).
    uint32_t glInternal; ///< internal format (sized, or the unsized legacy one for A8/L8/A8L8)
    uint32_t glFormat;
    uint32_t glType;
    uint8_t glBytes;     ///< bytes per pixel of the data handed to GL (uncompressed)
};

const FormatInfo *GetFormatInfo(D3DFORMAT format);
inline bool IsCompressed(D3DFORMAT f) { const FormatInfo *i = GetFormatInfo(f); return i && i->blockBytes != 0; }

/// Bytes per row (or per row of 4x4 blocks for compressed formats).
uint32_t FormatPitch(D3DFORMAT format, uint32_t width);
/// Rows in memory for a surface of the given height (block rows for DXT).
uint32_t FormatRows(D3DFORMAT format, uint32_t height);
inline uint32_t FormatLevelSize(D3DFORMAT f, uint32_t w, uint32_t h) { return FormatPitch(f, w) * FormatRows(f, h); }

/// Number of mip levels of a complete chain.
uint32_t FullMipCount(uint32_t w, uint32_t h, uint32_t d = 1);

/// Reads one pixel as D3DFMT_A8R8G8B8. Not for compressed formats.
uint32_t UnpackPixel(D3DFORMAT format, const uint8_t *p);
/// Writes one D3DFMT_A8R8G8B8 pixel. Not for compressed formats.
void PackPixel(D3DFORMAT format, uint32_t argb, uint8_t *p);

/// Whether the DXT data of a texture is decoded to RGBA8 on the CPU instead of being handed to the GPU compressed
/// (TextureDesc::decodeDxt, the decision of both backends): without S3TC / BC support (`s3tc` false), when the top level
/// is not made of whole 4x4 blocks (WEBGL_compressed_texture_s3tc and WebGPU's BC formats require a multiple of 4 there;
/// the smaller mip levels of such a texture may be 1 or 2 pixels), and for volume textures (neither WebGL2 nor WebGPU
/// without an optional feature has 3D S3TC / BC textures). False for formats that are not block compressed.
bool DecodeDxtOnCpu(const FormatInfo &info, uint32_t width, uint32_t height, bool volume, bool s3tc);

/// Decodes DXT1-5 to 4 bytes per pixel, either D3D order (B,G,R,A) or GL RGBA.
void DecodeDXT(D3DFORMAT format, const uint8_t *src, uint32_t srcPitch, uint32_t w, uint32_t h,
               uint8_t *dst, uint32_t dstPitch, bool rgba);
/// DecodeDXT for the `depth` slices of a volume level (each slice FormatRows(h) block rows), written one after the other
/// to `dst` with rows of w * 4 bytes.
void DecodeDXTVolume(D3DFORMAT format, const uint8_t *src, uint32_t srcPitch, uint32_t w, uint32_t h, uint32_t depth,
                     uint8_t *dst, bool rgba);
/// Encodes A8R8G8B8 pixels to DXT1/3/5 (simple range-fit compressor).
bool EncodeDXT(D3DFORMAT format, const uint8_t *srcArgb, uint32_t srcPitch, uint32_t w, uint32_t h,
               uint8_t *dst, uint32_t dstPitch);

/// Converts a rectangle between two D3D formats (any combination except
/// block compressed destinations of unsupported kinds). Returns false when the
/// conversion is not possible.
bool ConvertPixels(D3DFORMAT srcFormat, const void *src, uint32_t srcPitch,
                   D3DFORMAT dstFormat, void *dst, uint32_t dstPitch,
                   uint32_t w, uint32_t h);

/// Converts a rectangle of D3D memory to the layout GL expects for
/// FormatInfo::glFormat/glType. `src` points at the first pixel of the
/// rectangle. If no conversion is needed and the rows are contiguous the
/// source pointer is returned, otherwise the data is placed in `scratch`.
/// For block compressed formats that must be decoded the result is RGBA8.
const uint8_t *PrepareForGL(const FormatInfo &info, const uint8_t *src, uint32_t srcPitch,
                            uint32_t w, uint32_t h, bool decodeDxt, std::vector<uint8_t> &scratch);

} // namespace dxwebgl2
