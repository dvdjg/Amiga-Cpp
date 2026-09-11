#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Hook prebuild de la demo 078 (lo invoca tools/build/build-demo.sh).
#
# Genera el blob UAF-R de la malla (cubo `obj2c`) que la demo incbina y consume en
# runtime via Blob + MeshAssetView, cerrando el ciclo exportador -> runtime en 3D.
#
# Uso: lo llama build-demo.sh automaticamente (cwd = raiz del repo).
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
mkdir -p "$ROOT/out/assets/uaf"
node "$ROOT/dist/tools/assets/uaf-pack.js" "$ROOT/out/assets/uaf/cube.uafr" --mesh
