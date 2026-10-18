#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Hook prebuild de la demo 304 (lo invoca tools/build/build-demo.sh).
#
# Regenera el **atlas de iconos del HUD** (H6) desde la hoja fuente con el pipeline
# canonico (`tools/assets/hud-atlas.mjs`); la demo incbina el .bin (iconos del
# inventario y fantasma del arrastre). Formato: HUD_SYSTEM §8.2.
#
# Uso: lo llama build-demo.sh automaticamente (cwd = raiz del repo).
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../../../.." && pwd)"
node "$ROOT/tools/assets/hud-atlas.mjs" "$ROOT/assets/amiga/hud/test_icons.png" \
	--out "$ROOT/out/assets/hud/test_icons"
