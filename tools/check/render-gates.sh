#!/usr/bin/env bash
# Render gates: host geometry + on-target BOB base/footprint.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"

bash tools/run-host-tests.sh tests/host/graphics/504_bob_plane_modulo tests/host/graphics/505_plane_bob_layer
bash tests/amiga/l1_backend/007_bob_plane_base/verify.sh "${1:-}"

echo "[render-gates] OK: HOST-504 + HOST-505 + L1-007"
