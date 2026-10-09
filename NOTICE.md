# Notices and third-party licenses

## dxWebGL2

Copyright (C) 2026 dxWebGL2 contributors. Licensed under the GNU General Public License, version 3 or (at your option)
any later version (`SPDX-License-Identifier: GPL-3.0-or-later`); the full text is in [`LICENSE`](LICENSE).

dxWebGL2 originated in the WebAssembly port of Command & Conquer Generals Zero Hour
(<https://github.com/scr2em/Generals-Zero-Hours-Web>), where it was written as `Dependencies/WebD3D8` to run the
game's Direct3D 8 renderer on WebGL2 in the browser. It was extracted into this standalone library; no other code of
that game is part of it. It is not affiliated with or endorsed by Electronic Arts, Microsoft or any other trademark
holder. "Direct3D", "DirectX" and "Windows" are trademarks of Microsoft Corporation; they are used here only to
name the API that is implemented.

## Third-party components

### Wine Direct3D 8 headers (`third_party/wine/include/d3d8.h`, `d3d8types.h`, `d3d8caps.h`)

* Copyright (C) 2002 Jason Edmeades and the Wine project authors.
* License: GNU LGPL version 2.1 or later (`third_party/wine/COPYING.LIB`), compatible with the GPL-3.0-or-later
  license of the rest of the library (LGPL-2.1 section 3).
* Source: <https://github.com/wine-mirror/wine>, commit `20315474eb2a129234d13a9d91376ec7ea752d4c`.
  Modified for dxWebGL2, see `third_party/wine/PROVENANCE.md` and `third_party/wine/patches/`.

### Not included

* Microsoft DirectX SDK headers (including the `min-dx8-sdk` excerpt) are **not** part of this repository and are not
  needed to build or use it. `include/dx8/d3dx8*.h` (the D3DX 8 subset) were written for dxWebGL2 from the public API
  documentation; no text of Microsoft's headers was copied (the names, values and signatures of an API are the
  facts that compatibility requires, and were checked against the real thing, see `docs/HEADERS.md`).
* The test tools use [Playwright](https://playwright.dev/) (Apache-2.0) and a Chromium browser from the developer's
  machine; neither is distributed with this repository.

## Tools used to build

Emscripten (MIT / University of Illinois) and its LLVM toolchain; they are not distributed with this repository and
the generated WebAssembly/JavaScript of your application contains parts of the Emscripten runtime under its licenses.
