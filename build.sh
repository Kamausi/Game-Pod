#!/usr/bin/env bash
# Build the WebAssembly game into web/build/. Requires an activated emsdk (emcmake on PATH).
set -euo pipefail
cd "$(dirname "$0")"
if ! command -v emcmake >/dev/null; then
  echo "emcmake not found. Install/activate emsdk: https://emscripten.org/docs/getting_started/downloads.html" >&2
  exit 1
fi
emcmake cmake -B build-wasm -DCMAKE_BUILD_TYPE=Release
cmake --build build-wasm -j
ls -lh web/build/
