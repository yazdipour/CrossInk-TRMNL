#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build/karakeep"
BINARY="$BUILD_DIR/KarakeepTest"
mkdir -p "$BUILD_DIR"

c++ -std=c++20 -O2 -Wall -Wextra -pedantic \
  -I"$ROOT_DIR" -I"$ROOT_DIR/lib" -I"$ROOT_DIR/lib/JsonParser" \
  "$ROOT_DIR/test/karakeep/KarakeepTest.cpp" \
  "$ROOT_DIR/lib/Karakeep/KarakeepBookmarkParser.cpp" \
  "$ROOT_DIR/lib/Karakeep/KarakeepContentConverter.cpp" \
  "$ROOT_DIR/lib/JsonParser/StreamingJsonParser.cpp" \
  -o "$BINARY"

"$BINARY"
