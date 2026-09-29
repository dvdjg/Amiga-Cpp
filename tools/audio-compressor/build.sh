#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="$ROOT/out/tmp/audio-compressor"
CXX="${CXX:-g++}"
mkdir -p "$OUT"
"$CXX" -std=gnu++23 -O2 -Wall -Wextra -Werror=narrowing \
	-I"$ROOT/engine/include" "$ROOT/tools/audio-compressor/src/main.cpp" \
	-o "$OUT/audio-compressor"
echo "$OUT/audio-compressor"
