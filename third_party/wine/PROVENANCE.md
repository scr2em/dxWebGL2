# Wine Direct3D 8 headers

`include/d3d8.h`, `include/d3d8types.h` and `include/d3d8caps.h` are the Direct3D 8 API declarations of the
[Wine](https://www.winehq.org/) project (hand written by the Wine developers from the public API, initially by Jason
Edmeades, 2002), with a small patch for dxWebGL2.

| | |
|---|---|
| Upstream | https://github.com/wine-mirror/wine (mirror of https://gitlab.winehq.org/wine/wine) |
| Commit | `20315474eb2a129234d13a9d91376ec7ea752d4c` (Wine 11.19 development tree, 2026-10-08) |
| Files | `include/d3d8.h`, `include/d3d8types.h`, `include/d3d8caps.h`, license text `COPYING.LIB` |
| License | GNU Lesser General Public License, version 2.1 **or (at your option) any later version**, as stated in the header of each file. The full text is in `COPYING.LIB`. |
| Compatibility with dxWebGL2 | LGPL-2.1-or-later may be combined with GPL-3.0-or-later code: section 3 of the LGPL lets a recipient apply the terms of the GPL (version 2 or any later version) to a copy of the library, so the combined work is distributed under GPL-3.0-or-later. The three files keep their own license notice and remain LGPL-2.1-or-later. |

mingw-w64 ships the same headers (`mingw-w64-headers/include/d3d8*.h`, imported from Wine, only `BOOL` is spelled
`WINBOOL` there); the Wine copy was vendored because it is the source of both.

Why these and not Microsoft's SDK headers: the DirectX SDK (and excerpts of it such as `min-dx8-sdk`) are Microsoft
software whose redistribution terms are unclear. Wine's headers are free software and source and binary compatible
with Microsoft's: [`docs/HEADERS.md`](../../docs/HEADERS.md) explains how that is verified.

## Changes made for dxWebGL2

`patches/0001-dxwebgl2-integration.patch` (apply with `patch -p1` to the three files):

* the Win32/COM base comes from `<dxwebgl2/win32_min.h>` instead of `<windows.h>` / `<objbase.h>`, so that the
  headers work without a Windows SDK and with a project's own Win32 compatibility layer;
* the 4-byte structure packing of 32-bit Windows is also selected on wasm32 (`DXWEBGL2_PACK4`), the Wine headers
  only do it for `__i386__`;
* `D3DFMT_FORCE_DWORD`, `D3DDEVTYPE_FORCE_DWORD`, `D3DSBT_FORCE_DWORD`, `D3DCUBEMAP_FACE_FORCE_DWORD` and
  `D3DSWAPEFFECT_FORCE_DWORD` are `0x7fffffff` like in Microsoft's headers (Wine has `0xffffffff` for some),
  `D3DVBF_FORCE_DWORD` was missing;
* declarations that Microsoft's headers have and Wine's lack were added at the end of `d3d8types.h` / near the top of
  `d3d8.h`: `D3DSP_REGNUM_MASK`, `D3DSP_REPLICATE{RED,GREEN,BLUE,ALPHA}`, `D3DPV_DONOTCOPYDATA`, `D3DRTYPECOUNT`,
  `D3DRESOURCESTATS`, `D3DDEVINFO_RESOURCEMANAGER`, `D3DDEVINFO_D3DVERTEXSTATS`, `D3DCURRENT_DISPLAY_MODE`,
  `D3DCREATE_DISABLE_DRIVER_MANAGEMENT`, `D3DPRESENT_BACK_BUFFERS_MAX`, `D3DSGR_*`, `D3DCURSOR_IMMEDIATE_UPDATE`;
* an SPDX line and a pointer to the patch at the top of each file.

## Updating

`tools/update_wine_headers.sh` re-creates the directory from the pinned commit and the patch (`--write` to overwrite,
`--commit SHA` to move the pin). After an update run `tools/check_header_compat.py` (see `docs/HEADERS.md`) and
`scripts/test.sh`.
