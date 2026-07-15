#!/usr/bin/env sh
set -eu

ROOT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/dev/build"
BINDINGS_DIR="$BUILD_DIR/bindings"
WIT_BINDGEN="${WIT_BINDGEN:-wit-bindgen}"
WASI_SDK_PATH="${WASI_SDK_PATH:-/tmp/wasi-sdk-33.0-x86_64-macos}"
if [ -x "$WASI_SDK_PATH/bin/clang" ]; then
  PATH="$WASI_SDK_PATH/bin:$PATH"
  CC="${CC:-$WASI_SDK_PATH/bin/clang}"
else
  CC="${CC:-/usr/bin/clang}"
fi
WASI_SYSROOT="${WASI_SYSROOT:-}"

if ! command -v "$WIT_BINDGEN" >/dev/null 2>&1 && [ -x "$HOME/.cargo/bin/wit-bindgen" ]; then
  WIT_BINDGEN="$HOME/.cargo/bin/wit-bindgen"
fi

if [ -z "$WASI_SYSROOT" ] && command -v brew >/dev/null 2>&1; then
  WASI_SYSROOT="$(brew --prefix wasi-libc 2>/dev/null || true)"
fi
if [ -n "$WASI_SYSROOT" ] && [ -d "$WASI_SYSROOT/share/wasi-sysroot" ]; then
  WASI_SYSROOT="$WASI_SYSROOT/share/wasi-sysroot"
fi
if [ -z "$WASI_SYSROOT" ] && [ -d "$WASI_SDK_PATH/share/wasi-sysroot" ]; then
  WASI_SYSROOT="$WASI_SDK_PATH/share/wasi-sysroot"
fi

mkdir -p "$BINDINGS_DIR" "$ROOT_DIR/wasm"

"$WIT_BINDGEN" c \
  --world paysplit \
  --out-dir "$BINDINGS_DIR" \
  "$ROOT_DIR/wasm/lnbits-extension.wit"

"$CC" \
  --target=wasm32-wasip1 \
  ${WASI_SYSROOT:+--sysroot="$WASI_SYSROOT"} \
  -O2 \
  -mexec-model=reactor \
  -I "$BINDINGS_DIR" \
  -I "$ROOT_DIR/dev/src" \
  "$ROOT_DIR/dev/src/paysplit.c" \
  "$BINDINGS_DIR"/*.c \
  "$BINDINGS_DIR"/*.o \
  -Wl,--export-all \
  -Wl,--allow-undefined \
  -o "$BUILD_DIR/paysplit.core.wasm"

npx --yes @bytecodealliance/jco embed \
  --wit "$ROOT_DIR/wasm/lnbits-extension.wit" \
  --world-name paysplit \
  "$BUILD_DIR/paysplit.core.wasm" \
  -o "$BUILD_DIR/paysplit.typed.wasm"

npx --yes @bytecodealliance/jco new \
  --wasi-reactor \
  "$BUILD_DIR/paysplit.typed.wasm" \
  -o "$ROOT_DIR/wasm/module.wasm"
