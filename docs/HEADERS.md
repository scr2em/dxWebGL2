# Direct3D headers, Win32 base types and ABI compatibility

dxWebGL2 implements the Direct3D 8 COM interfaces, so applications compile against `<d3d8.h>` and `<d3dx8.h>` and
create the device with `Direct3DCreate8()`. Those headers are part of this repository, they are **not** Microsoft's.

## Where the headers come from

| Header | Origin | License |
|---|---|---|
| `d3d8.h`, `d3d8types.h`, `d3d8caps.h` | [Wine](https://www.winehq.org/), commit `20315474eb2a129234d13a9d91376ec7ea752d4c`, patched (`third_party/wine/`) | LGPL-2.1-or-later (combined with the GPL-3.0-or-later of the rest, LGPL-2.1 section 3) |
| `d3dx8.h`, `d3dx8math.h`, `d3dx8math.inl`, `d3dx8core.h`, `d3dx8tex.h` | written for dxWebGL2 from the public API documentation (`include/dx8/`) | GPL-3.0-or-later |
| `dxwebgl2/win32_min.h` | written for dxWebGL2 | GPL-3.0-or-later |

Microsoft's DirectX SDK headers (and the `min-dx8-sdk` excerpt) were deliberately **not** used: their redistribution
terms are unclear. mingw-w64's `d3d8*.h` are the same files as Wine's (imported from there), see
`third_party/wine/PROVENANCE.md`. Wine and mingw-w64 have no D3DX 8 headers (only D3DX 9), hence the own ones.

The D3DX 8 subset is what the library implements, see [COVERAGE.md](COVERAGE.md): math (complete), `ID3DXBuffer`, the
shader assembler, error strings, texture/surface creation, loading and filtering, `ID3DXMatrixStack`. Meshes, shapes,
effects, fonts and sprites are not declared.

## Is it really compatible?

Code written for Microsoft's headers must compile unchanged and agree on every binary detail: the vtable order of every
interface, structure sizes and member offsets, the value of every constant and enumerator. This is verified three
ways:

1. `tools/check_header_compat.py --reference-dir DIR` compares the headers with any Direct3D 8 header set (given
   a copy of the SDK or of the `min-dx8-sdk` excerpt): it parses all COM interfaces (method names, order, parameter
   types), and compiles and runs a probe program for both sets that prints the size of every structure, the offset of
   every member and the value of every constant. When this was done for the extraction, 12 interfaces and 1080 values
   were compared, with one known difference that is harmless (`D3DVSD_TOKENTYPEMASK` is an unsigned constant in
   Wine's headers and a negative `int` in Microsoft's; it is only ever masked with DWORD tokens).
2. `tests/unit/test_header_layout.cpp` includes `tests/data/d3d8_abi_expected.inc`, the output of that comparison
   (the numbers of the reference for wasm32, with the 4-byte packing of 32-bit Windows), as compile time
   assertions. A few examples that are there: `sizeof(D3DCAPS8) == 212`, `sizeof(D3DPRESENT_PARAMETERS) == 52`,
   `sizeof(D3DSURFACE_DESC) == 32`, `sizeof(D3DLIGHT8) == 104`, `sizeof(D3DMATERIAL8) == 68`,
   `sizeof(D3DVIEWPORT8) == 24`, `sizeof(D3DADAPTER_IDENTIFIER8) == 1068`. If the test builds, the layout is right.
3. `tools/check_header_compat.py --check-golden tests/data/d3d8_interfaces.golden` (a test in `scripts/test.sh`) fails
   when any interface's method order or signature changes.

The library itself implements the interfaces with `override` on every method, so any mismatch between the headers and
the implementation is a compile error.

## The Win32 base: `dxwebgl2/win32_min.h`

The Direct3D headers need a few Windows types (`DWORD`, `HRESULT`, `RECT`, `GUID`, `LARGE_INTEGER`, `PALETTEENTRY`,
`RGNDATA`, the opaque handles) and the C++ COM macros (`STDMETHOD`, `DECLARE_INTERFACE_`, `IUnknown`). `win32_min.h`
provides exactly those, with the sizes of 32-bit Windows (`DWORD` is `unsigned long`, 32 bits on wasm32).

**Standalone** (default): nothing else is needed, no `windows.h`.

**With an existing Win32 compatibility layer** (or a real Windows SDK): the project's own definitions must win.

* If `<windows.h>` was included before any dxWebGL2 header, it is detected through its include guard
  (`_WINDOWS_`, `_WINDEF_`, `WEBCOMPAT_WINDOWS_H`, `__WINE_WINDOWS_H`, `_WIN32`).
* Otherwise define `DXWEBGL2_HAVE_WINDOWS_H` (CMake: `-DDXWEBGL2_USE_PROJECT_WINDOWS_H=ON`, and
  `-DDXWEBGL2_WINDOWS_H_TARGET=<interface target>` naming the target that carries the include directories and flags
  of that `windows.h`; the target is linked to everything that links dxWebGL2).

In that mode `win32_min.h` only includes `<windows.h>` and `<objbase.h>` and adds the macros that such headers often
lack (`__MSABI_LONG`, `DUMMYUNIONNAME*`, `TRUE`/`FALSE`); it defines no type, so nothing can clash.
`tests/integration/` builds against a stand-in `windows.h` with its own spelling of everything to prove that, and
the library was also checked against the Win32 layer of the game port it came from (see the extraction notes in
[SYNC.md](SYNC.md)).

Two things such a project must keep consistent with the library:

* **`WCHAR` is a 16-bit UTF-16 code unit**, whatever the size of `wchar_t`. In standalone mode it is `wchar_t` when
  that has 16 bits (`-fshort-wchar`) and `char16_t` otherwise. The library never calls a `wcs*` function and converts
  the `W` entry points' file names from UTF-16 itself (`tests/unit/test_wide_strings.cpp` runs with both
  `wchar_t` sizes against the same library binary).
* **Structure packing**: the headers select 4-byte packing on wasm32 like 32-bit Windows (`DXWEBGL2_PACK4`), so
  structures such as `D3DCAPS8` have the layout that code written for 32-bit Windows expects. A project that
  compiles *its own* code with a different packing for these structures would break the ABI; do not.

## Using only the headers

The CMake target `dxWebGL2::d3d8_headers` provides just the Direct3D headers and `win32_min.h` (for example to
compile code that is later linked with a different implementation).
