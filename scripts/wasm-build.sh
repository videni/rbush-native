#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
EMXX="${EMXX:-$ROOT/.tools/emsdk/upstream/emscripten/em++}"
if [[ ! -x "$EMXX" ]]; then
  echo "Emscripten not found. Install official emsdk 4.0.20 locally or set EMXX." >&2
  exit 1
fi
mkdir -p "$ROOT/wasm/dist"
if [[ -z "${EMSDK_PYTHON:-}" ]]; then
  for candidate in python3.14 python3.13 python3.12 python3.11 python3.10 python3; do
    if command -v "$candidate" >/dev/null 2>&1 && "$candidate" -c 'import sys; raise SystemExit(sys.version_info < (3, 10))'; then
      export EMSDK_PYTHON="$(command -v "$candidate")"
      break
    fi
  done
fi
"$EMXX" --version | head -n 1
"$EMXX" -std=c++17 -O3 -fexceptions -I"$ROOT/cpp/include" \
  "$ROOT/cpp/src/rbush_c.cpp" "$ROOT/wasm/bridge.cpp" \
  -sMODULARIZE=1 -sEXPORT_ES6=1 -sENVIRONMENT=web,node \
  -sALLOW_MEMORY_GROWTH=1 -sALLOW_TABLE_GROWTH=1 -sWASM_BIGINT=1 \
  -sDISABLE_EXCEPTION_CATCHING=0 \
  -sEXPORTED_RUNTIME_METHODS='["UTF8ToString","addFunction","removeFunction","HEAPU8"]' \
  -sEXPORTED_FUNCTIONS='["_malloc","_free","_rbw_create","_rbush_destroy","_rbush_clear","_rbush_set_comparators","_rbw_insert","_rbush_load","_rbw_remove","_rbush_all_result","_rbw_search_result","_rbush_result_count","_rbush_result_data","_rbush_result_destroy","_rbw_collides","_rbush_export_counts","_rbush_to_json","_rbush_from_json","_rbush_last_error"]' \
  -o "$ROOT/wasm/dist/rbush.mjs"
