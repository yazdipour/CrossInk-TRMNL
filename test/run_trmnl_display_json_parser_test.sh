#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build/trmnl_display_json_parser"
BINARY="$BUILD_DIR/TrmnlDisplayJsonParserTest"

mkdir -p "$BUILD_DIR"

SOURCES=(
  "$ROOT_DIR/test/trmnl_display_json_parser/TrmnlDisplayJsonParserTest.cpp"
  "$ROOT_DIR/lib/JsonParser/TrmnlDisplayJsonParser.cpp"
  "$ROOT_DIR/lib/JsonParser/StreamingJsonParser.cpp"
)

CXXFLAGS=(
  -std=c++20
  -O2
  -Wall
  -Wextra
  -pedantic
  -I"$ROOT_DIR"
  -I"$ROOT_DIR/lib"
  -I"$ROOT_DIR/lib/JsonParser"
  -I"$ROOT_DIR/src"
)

c++ "${CXXFLAGS[@]}" "${SOURCES[@]}" -o "$BINARY"

"$BINARY" "$@"
