# Textured, lit cube

Fixed-function Direct3D 8: a vertex buffer and an index buffer holding a cube, a procedurally generated texture
with a full mip chain, one directional light plus an ambient light, depth testing and the texture stage
cascade (`texture * diffuse`).

```
source /path/to/emsdk/emsdk_env.sh
cd dxwebgl2
emcmake cmake -S . -B build -G Ninja
cmake --build build --target textured_cube
python3 -m http.server -d build/examples 8000      # then open http://localhost:8000/textured_cube.html
```

The page needs a browser with WebGL2 (developed and tested with Chrome/Chromium). Configured with
`-DDXWEBGL2_WEBGPU=ON`, the example renders through WebGPU when the browser offers it (the default backend,
`DXWEBGL2_BACKEND_AUTO`) and through WebGL2 otherwise; `?args=-dxwebgl2-backend=webgl2` selects WebGL2.

What to look at in `main.cpp`:

* `CreateTexture()` creates a managed texture (`D3DPOOL_MANAGED`), locks every mip level and writes pixels.
  The library keeps the system-memory copy and uploads to WebGL2 when the texture is used.
* `CreateCube()` fills `D3DUSAGE_WRITEONLY` buffers through `Lock`/`Unlock`; the data reaches the GPU lazily,
  right before the draw call.
* The fixed-function pipeline (lighting, texture stages, fog, ...) is translated to GLSL ES 3.00 shaders that are
  generated per combination of render states and cached.
