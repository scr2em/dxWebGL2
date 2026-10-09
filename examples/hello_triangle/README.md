# Hello triangle

The smallest Direct3D 8 program with a visible result: a vertex-colored triangle that spins around the Y axis.
It shows the whole life of a device on the web: `Direct3DCreate8`, `CreateDevice` (no window handle needed),
transforms, `DrawPrimitiveUP`, `Present`.

```
source /path/to/emsdk/emsdk_env.sh
cd dxwebgl2
emcmake cmake -S . -B build -G Ninja
cmake --build build --target hello_triangle
python3 -m http.server -d build/examples 8000      # then open http://localhost:8000/hello_triangle.html
```

The page needs a browser with WebGL2 (developed and tested with Chrome/Chromium).

Things worth knowing:

* `dxwebgl2_SetPresentMode(DXWEBGL2_PRESENT_IMPLICIT)` makes the browser present the canvas when the frame
  callback returns, which is what `emscripten_set_main_loop` gives you. The default (explicit swap control) is
  meant for code that renders from a thread that never returns to the browser, see the README of the library.
* `?args=--frames=60` in the URL stops the demo after 60 frames (used by the automated test).
