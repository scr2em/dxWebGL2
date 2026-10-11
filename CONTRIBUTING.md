# Contributing to dxWebGL2

Thank you for helping. dxWebGL2 is GPL-3.0-or-later; by contributing you agree that your contribution is licensed under
the same terms.

## Ground rules

* **Emscripten only.** The build refuses to configure with another toolchain.
* **Keep the layers** ([docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)): API independent code goes into `src/core`
  (no `IDirect3D*` types there), API specific objects into `src/d3d8`, and every WebGL call into the WebGL2 backend
  (`src/core/gl`), every WebGPU call into the WebGPU backend (`src/core/webgpu`): the front end and the rest of the core
  reach the GPU through `core/backend.h` only ([docs/WEBGPU.md](docs/WEBGPU.md)). `tools/check_layering.py` enforces it.
* **Both backends.** A rendering change is made in both (GLSL and WGSL generation share their decisions, see
  [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)) and tested on both: `scripts/test.sh` runs every browser test on WebGL2
  and on WebGPU and compares their pictures.
* **Name the backend in tests.** The default backend (`DXWEBGL2_BACKEND_AUTO`) selects WebGPU in the test browser, so a
  runner started by hand without `--backend=` tests WebGPU: pass `--backend=webgl2` for WebGL2. `scripts/test.sh`
  names the backend of every run, then checks AUTO itself.
* **Do not add Microsoft SDK headers or code**, not even excerpts (`d3d8.h` of the DirectX SDK, `min-dx8-sdk`, sample
  code). Write from the public documentation and compare behavior with `tools/check_header_compat.py`
  if you have a legitimate copy of the SDK.
* **SPDX headers** in every source file: `SPDX-License-Identifier: GPL-3.0-or-later` and
  `Copyright (C) 2026 dxWebGL2 contributors` (`tools/check_spdx.py` checks, new files can copy the header of any file).
* Third-party code needs a compatible license, its license file under `third_party/<name>/`, an entry in
  [NOTICE.md](NOTICE.md), and a provenance file stating the exact upstream version.
* Generic wording in comments: describe *what* the code emulates and *why*, not which application needed it.

## Building and testing

```sh
source /path/to/emsdk/emsdk_env.sh
scripts/test.sh                 # build + every test (see below)
scripts/test.sh --no-browser    # without headless Chromium
```

Tests, by directory:

| Directory | What | Runs in |
|---|---|---|
| `tests/unit/test_header_layout.cpp` | ABI of the headers (compile-time asserts from `tests/data/d3d8_abi_expected.inc`) | node |
| `tests/unit/test_d3dx_math.cpp` | D3DX math, matrix stack | node |
| `tests/unit/test_wide_strings.cpp` | `W` entry points with 16- and 32-bit `wchar_t` | node |
| `tests/integration/` | a project with its own `windows.h` | node |
| `tests/runtime/d3d8_runtime_test.cpp` | API checks and rendering scenes, probes compared with `expected_probes.inc`; prints the GPU API call counts per category (`STATS` lines); `--perf=N` times N draws per frame | headless Chromium |
| `tests/runtime/d3d8_features_test.cpp` | feature scenes with checked pixels: border colours, provoking vertex, ps.1.x instructions, D3DX filters, state caching, clears of rectangles, fans / points / sprites / fill modes, odd vertex strides, formats (DXT sizes that are not a multiple of 4, DXT volumes), mid-frame buffer and texture updates, render target read-back, diagnostics, context loss, multisampling (`tests/runner/run_features.mjs`, 7 modes) | headless Chromium |
| `tests/runner/run_regression.mjs` | frame hashes of the runtime test scenes against `tests/runner/reference_hashes.json`, per backend (`--update` after an intended rendering change; review the pictures first) | headless Chromium |
| `tests/runner/compare_backends.mjs` | the runtime test scenes rendered by WebGL2 and WebGPU, compared with a tolerance (difference pictures with `--out`) | headless Chromium |
| `tests/runner/run_auto.mjs`, `run_regression.mjs --backend=auto` | the default backend (`DXWEBGL2_BACKEND_AUTO`): the frame hashes of the backend it must select in browsers with and without WebGPU; the pages of `tests/consumer/device.cpp` (package with WebGPU, with and without JSPI, sources without WebGPU) in browsers with WebGPU, without an adapter, without `navigator.gpu`, with a failing `requestDevice` | headless Chromium |
| `examples/` | each example is run for 40 frames and its pixels checked | headless Chromium |
| `tools/check_*.py` | SPDX, layering, interface golden file | python |

### When you change rendering on purpose

The runtime test compares probe pixels (tolerance 6/channel) with recorded values. After an intended change, review the
screenshots (`build/shot*.png`, `build/shot*_webgpu.png`, `build/compare/*-diff.png`), then re-record:
`node tests/runner/record_probes.mjs build/tests` (software rasterizer recommended), and commit
`tests/runtime/expected_probes.inc` with an explanation (recorded on WebGL2); the frame hashes with
`node tests/runner/run_regression.mjs build/tests --update --backend=webgl2|webgpu`.

### When you change a public header

`tests/data/d3d8_interfaces.golden` and `tests/data/d3d8_abi_expected.inc` pin the Direct3D 8 ABI. They must **not**
change unless the Direct3D 8 API itself was wrong; if it was, regenerate them with
`tools/check_header_compat.py --reference-dir <sdk> --emit-expected ... --emit-golden ...` (see docs/HEADERS.md).

## Style

C++20 in the library (public headers stay C++11 and C where the Direct3D headers are), 4 spaces, braces on their own
line for functions and types as in the existing code, no tabs, lines up to ~130 columns. Comments explain intent
and the Direct3D/GL semantics being emulated. Prefer small functions and the existing helpers (`Min`, `Max`, `Clamp`,
`Log`, `DXWEBGL2_UNSUPPORTED`).

## The game port sync

Until the Command & Conquer Generals Zero Hour web port switches to this library, `tools/sync_from_game.sh` brings
changes from its `Dependencies/WebD3D8` over, see [docs/SYNC.md](docs/SYNC.md). Hand edits in the imported files
are fine (the sync merges three-way), but prefer extending `tools/sync/sync_rules.py` for mechanical edits.

## Reporting problems

Please include: browser and version, `dxwebgl2_GetRendererString()`, the console lines starting with `[dxWebGL2]`
(`dxwebgl2_SetDebug(1)` adds more), and if possible the smallest D3D8 call sequence that shows it (the runtime test is
a good starting point for a regression test).
