#!/usr/bin/env sh
set -eu

cd "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"

WASI_SDK_VERSION="${WASI_SDK_VERSION:-33}"
WASI_SDK_RELEASE="${WASI_SDK_RELEASE:-33.0}"
WASI_SDK_DIR="${WASI_SDK_DIR:-/tmp/wasi-sdk-${WASI_SDK_RELEASE}-x86_64-macos}"
WASI_SDK_ARCHIVE="/tmp/wasi-sdk-${WASI_SDK_RELEASE}-x86_64-macos.tar.gz"
WASI_SDK_URL="https://github.com/WebAssembly/wasi-sdk/releases/download/wasi-sdk-${WASI_SDK_VERSION}/wasi-sdk-${WASI_SDK_RELEASE}-x86_64-macos.tar.gz"

if ! command -v rustup >/dev/null 2>&1; then
  if ! command -v brew >/dev/null 2>&1; then
    echo "Homebrew is required to install rustup: https://brew.sh" >&2
    exit 1
  fi
  brew install rustup
fi

rustup default stable

CARGO="$(rustup which cargo)"
RUSTC="$(rustup which rustc)"
TOOLCHAIN_BIN="$(dirname "$CARGO")"
PATH="$HOME/.cargo/bin:$TOOLCHAIN_BIN:$PATH"

RUSTC="$RUSTC" "$CARGO" install wit-bindgen-cli

if [ ! -x "$WASI_SDK_DIR/bin/clang" ]; then
  curl -L --fail -o "$WASI_SDK_ARCHIVE" "$WASI_SDK_URL"
  tar -xzf "$WASI_SDK_ARCHIVE" -C /tmp
fi

PATH="$WASI_SDK_DIR/bin:$PATH" \
  CC="$WASI_SDK_DIR/bin/clang" \
  WASI_SYSROOT="$WASI_SDK_DIR/share/wasi-sysroot" \
  ./build.sh
