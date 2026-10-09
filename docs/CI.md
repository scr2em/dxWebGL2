# Continuous integration

The repository deliberately contains **no CI workflow files** (nothing runs on GitHub Actions by itself). This page
describes how to set it up when you want it; everything a CI job needs is `scripts/test.sh`.

## What a job has to provide

| Need | How |
|---|---|
| Emscripten | the `emscripten/emsdk` container image, or `emsdk install <version> && emsdk activate <version>` and `source emsdk_env.sh` (set `EMSDK` so that `scripts/test.sh` can find it) |
| CMake >= 3.20, Ninja | distribution packages |
| node >= 18, Python 3 | distribution packages |
| Playwright + Chromium (for the browser tests) | `npm install -g playwright` and `npx playwright install --with-deps chromium`; export `NODE_PATH=$(npm root -g)` and, if the browser is not in Playwright's default cache, `PLAYWRIGHT_BROWSERS_PATH` or `CHROMIUM_PATH` |

No GPU is needed: the runner starts Chromium with `--use-angle=swiftshader --enable-unsafe-swiftshader`, which provides
a software WebGL2 implementation. `tests/data` and the recorded probes (`tests/runtime/expected_probes.inc`) were
recorded with that configuration; probes are compared with a tolerance of 6 per channel.

## Suggested workflow (not committed)

Create `.github/workflows/ci.yml` yourself when you want it. For example:

```yaml
name: ci
on:
  pull_request:
  push:
    branches: [main]
jobs:
  test:
    runs-on: ubuntu-24.04
    container: emscripten/emsdk:latest        # pin a version in real use
    steps:
      - uses: actions/checkout@v4
      - name: Tools
        run: |
          apt-get update && apt-get install -y ninja-build python3 nodejs npm
          npm install -g playwright
          npx playwright install --with-deps chromium
      - name: Build and test
        run: |
          export NODE_PATH="$(npm root -g)"
          scripts/test.sh --clean
```

The same job without the browser (`scripts/test.sh --no-browser`) takes about two minutes less and still checks the
header ABI, the D3DX math, wide-string handling, the project-`windows.h` integration, the layering rule and SPDX headers.

## Tuning

* `--jobs N` limits the parallelism of the build.
* Cache the Emscripten cache directory (`$EMSDK/upstream/emscripten/cache`) and the `build/` directory between runs.
* A matrix over Emscripten versions is useful: the library uses only stable Emscripten APIs (`emscripten_webgl_*`,
  `html5.h`), the tests additionally use `-sJSPI` and `-sPROXY_TO_PTHREAD`.
* Browser tests are the slow, occasionally flaky part (browser start-up); `tests/runner/run_test.mjs` has a 90 s
  timeout per run and exits with status 2 on a timeout, 1 on a failed check.

## Releasing

Tag `vX.Y.Z`, keep `project(dxWebGL2 VERSION ...)` and `DXWEBGL2_VERSION_*` in `include/dxwebgl2/dxwebgl2.h` in step
(the package version file uses the CMake version). Consumers pin the tag with `FetchContent` or install the package
(`cmake --install build --prefix <dir>`).
