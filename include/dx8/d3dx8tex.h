/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copyright (C) 2026 dxWebGL2 contributors
 *
 * This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
 * See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
 */
/*
 * d3dx8tex.h
 *
 * D3DX8 texture and surface helpers: requirement checks, creation, loading of
 * DDS/TGA/BMP images, scaling/format conversion between surfaces and mip
 * chain generation. Implemented in src/d3d8/d3dx_texture.cpp.
 */
#include "d3dx8.h"

#ifndef __D3DX8TEX_H__
#define __D3DX8TEX_H__


/* D3DX_FILTER flags for the image loading and mip filtering functions. */
#define D3DX_FILTER_NONE (1 << 0)
#define D3DX_FILTER_POINT (2 << 0)
#define D3DX_FILTER_LINEAR (3 << 0)
#define D3DX_FILTER_TRIANGLE (4 << 0)
#define D3DX_FILTER_BOX (5 << 0)

#define D3DX_FILTER_MIRROR_U (1 << 16)
#define D3DX_FILTER_MIRROR_V (2 << 16)
#define D3DX_FILTER_MIRROR_W (4 << 16)
#define D3DX_FILTER_MIRROR (7 << 16)

#define D3DX_FILTER_DITHER (8 << 16)

/* Image file formats. */
typedef enum _D3DXIMAGE_FILEFORMAT {
    D3DXIFF_BMP = 0,
    D3DXIFF_JPG = 1,
    D3DXIFF_TGA = 2,
    D3DXIFF_PNG = 3,
    D3DXIFF_DDS = 4,
    D3DXIFF_PPM = 5,
    D3DXIFF_DIB = 6,
    D3DXIFF_FORCE_DWORD = 0x7fffffff
} D3DXIMAGE_FILEFORMAT;

/* Description of an image file. */
typedef struct _D3DXIMAGE_INFO {
    UINT Width;
    UINT Height;
    UINT Depth;
    UINT MipLevels;
    D3DFORMAT Format;
    D3DRESOURCETYPE ResourceType;
    D3DXIMAGE_FILEFORMAT ImageFileFormat;
} D3DXIMAGE_INFO;

#ifdef __cplusplus

/* Callback of D3DXFillTexture: computes the color of one texel. */
typedef VOID (*LPD3DXFILL2D)(D3DXVECTOR4 *pOut, D3DXVECTOR2 *pTexCoord, D3DXVECTOR2 *pTexelSize, LPVOID pData);

extern "C" {

/*
 * Texture creation parameters. Returns the nearest values the device can
 * create (D3DX_DEFAULT arguments are resolved).
 */
HRESULT WINAPI D3DXCheckTextureRequirements(LPDIRECT3DDEVICE8 pDevice, UINT *pWidth, UINT *pHeight, UINT *pNumMipLevels,
                                            DWORD Usage, D3DFORMAT *pFormat, D3DPOOL Pool);
HRESULT WINAPI D3DXCheckCubeTextureRequirements(LPDIRECT3DDEVICE8 pDevice, UINT *pSize, UINT *pNumMipLevels, DWORD Usage,
                                                D3DFORMAT *pFormat, D3DPOOL Pool);
HRESULT WINAPI D3DXCheckVolumeTextureRequirements(LPDIRECT3DDEVICE8 pDevice, UINT *pWidth, UINT *pHeight, UINT *pDepth,
                                                  UINT *pNumMipLevels, DWORD Usage, D3DFORMAT *pFormat, D3DPOOL Pool);

/* Texture creation. */
HRESULT WINAPI D3DXCreateTexture(LPDIRECT3DDEVICE8 pDevice, UINT Width, UINT Height, UINT MipLevels, DWORD Usage,
                                 D3DFORMAT Format, D3DPOOL Pool, LPDIRECT3DTEXTURE8 *ppTexture);
HRESULT WINAPI D3DXCreateCubeTexture(LPDIRECT3DDEVICE8 pDevice, UINT Size, UINT MipLevels, DWORD Usage, D3DFORMAT Format,
                                     D3DPOOL Pool, LPDIRECT3DCUBETEXTURE8 *ppCubeTexture);
HRESULT WINAPI D3DXCreateVolumeTexture(LPDIRECT3DDEVICE8 pDevice, UINT Width, UINT Height, UINT Depth, UINT MipLevels,
                                       DWORD Usage, D3DFORMAT Format, D3DPOOL Pool,
                                       LPDIRECT3DVOLUMETEXTURE8 *ppVolumeTexture);

/* Surfaces. */
HRESULT WINAPI D3DXLoadSurfaceFromSurface(LPDIRECT3DSURFACE8 pDestSurface, const PALETTEENTRY *pDestPalette,
                                          const RECT *pDestRect, LPDIRECT3DSURFACE8 pSrcSurface,
                                          const PALETTEENTRY *pSrcPalette, const RECT *pSrcRect, DWORD Filter,
                                          D3DCOLOR ColorKey);
HRESULT WINAPI D3DXLoadSurfaceFromMemory(LPDIRECT3DSURFACE8 pDestSurface, const PALETTEENTRY *pDestPalette,
                                         const RECT *pDestRect, LPCVOID pSrcMemory, D3DFORMAT SrcFormat, UINT SrcPitch,
                                         const PALETTEENTRY *pSrcPalette, const RECT *pSrcRect, DWORD Filter,
                                         D3DCOLOR ColorKey);

/* Image files (DDS, TGA, BMP). */
HRESULT WINAPI D3DXGetImageInfoFromFileInMemory(LPCVOID pSrcData, UINT SrcDataSize, D3DXIMAGE_INFO *pSrcInfo);
HRESULT WINAPI D3DXGetImageInfoFromFileA(LPCSTR pSrcFile, D3DXIMAGE_INFO *pSrcInfo);
HRESULT WINAPI D3DXGetImageInfoFromFileW(LPCWSTR pSrcFile, D3DXIMAGE_INFO *pSrcInfo);
HRESULT WINAPI D3DXLoadSurfaceFromFileInMemory(LPDIRECT3DSURFACE8 pDestSurface, const PALETTEENTRY *pDestPalette,
                                               const RECT *pDestRect, LPCVOID pSrcData, UINT SrcData,
                                               const RECT *pSrcRect, DWORD Filter, D3DCOLOR ColorKey,
                                               D3DXIMAGE_INFO *pSrcInfo);
HRESULT WINAPI D3DXLoadSurfaceFromFileA(LPDIRECT3DSURFACE8 pDestSurface, const PALETTEENTRY *pDestPalette,
                                        const RECT *pDestRect, LPCSTR pSrcFile, const RECT *pSrcRect, DWORD Filter,
                                        D3DCOLOR ColorKey, D3DXIMAGE_INFO *pSrcInfo);
HRESULT WINAPI D3DXLoadSurfaceFromFileW(LPDIRECT3DSURFACE8 pDestSurface, const PALETTEENTRY *pDestPalette,
                                        const RECT *pDestRect, LPCWSTR pSrcFile, const RECT *pSrcRect, DWORD Filter,
                                        D3DCOLOR ColorKey, D3DXIMAGE_INFO *pSrcInfo);

HRESULT WINAPI D3DXCreateTextureFromFileInMemory(LPDIRECT3DDEVICE8 pDevice, LPCVOID pSrcData, UINT SrcDataSize,
                                                 LPDIRECT3DTEXTURE8 *ppTexture);
HRESULT WINAPI D3DXCreateTextureFromFileInMemoryEx(LPDIRECT3DDEVICE8 pDevice, LPCVOID pSrcData, UINT SrcDataSize,
                                                   UINT Width, UINT Height, UINT MipLevels, DWORD Usage,
                                                   D3DFORMAT Format, D3DPOOL Pool, DWORD Filter, DWORD MipFilter,
                                                   D3DCOLOR ColorKey, D3DXIMAGE_INFO *pSrcInfo,
                                                   PALETTEENTRY *pPalette, LPDIRECT3DTEXTURE8 *ppTexture);
HRESULT WINAPI D3DXCreateTextureFromFileA(LPDIRECT3DDEVICE8 pDevice, LPCSTR pSrcFile, LPDIRECT3DTEXTURE8 *ppTexture);
HRESULT WINAPI D3DXCreateTextureFromFileW(LPDIRECT3DDEVICE8 pDevice, LPCWSTR pSrcFile, LPDIRECT3DTEXTURE8 *ppTexture);
HRESULT WINAPI D3DXCreateTextureFromFileExA(LPDIRECT3DDEVICE8 pDevice, LPCSTR pSrcFile, UINT Width, UINT Height,
                                            UINT MipLevels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, DWORD Filter,
                                            DWORD MipFilter, D3DCOLOR ColorKey, D3DXIMAGE_INFO *pSrcInfo,
                                            PALETTEENTRY *pPalette, LPDIRECT3DTEXTURE8 *ppTexture);
HRESULT WINAPI D3DXCreateTextureFromFileExW(LPDIRECT3DDEVICE8 pDevice, LPCWSTR pSrcFile, UINT Width, UINT Height,
                                            UINT MipLevels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, DWORD Filter,
                                            DWORD MipFilter, D3DCOLOR ColorKey, D3DXIMAGE_INFO *pSrcInfo,
                                            PALETTEENTRY *pPalette, LPDIRECT3DTEXTURE8 *ppTexture);

#ifdef UNICODE
#define D3DXGetImageInfoFromFile D3DXGetImageInfoFromFileW
#define D3DXLoadSurfaceFromFile D3DXLoadSurfaceFromFileW
#define D3DXCreateTextureFromFile D3DXCreateTextureFromFileW
#define D3DXCreateTextureFromFileEx D3DXCreateTextureFromFileExW
#else
#define D3DXGetImageInfoFromFile D3DXGetImageInfoFromFileA
#define D3DXLoadSurfaceFromFile D3DXLoadSurfaceFromFileA
#define D3DXCreateTextureFromFile D3DXCreateTextureFromFileA
#define D3DXCreateTextureFromFileEx D3DXCreateTextureFromFileExA
#endif

/* Mip chain generation and procedural filling. */
HRESULT WINAPI D3DXFilterTexture(LPDIRECT3DBASETEXTURE8 pBaseTexture, const PALETTEENTRY *pPalette, UINT SrcLevel,
                                 DWORD Filter);
HRESULT WINAPI D3DXFillTexture(LPDIRECT3DTEXTURE8 pTexture, LPD3DXFILL2D pFunction, LPVOID pData);

} /* extern "C" */

#endif /* __cplusplus */

#endif /* __D3DX8TEX_H__ */
