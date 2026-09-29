#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Genera los `.raw` de audio que consumen las demos del mixer, de forma
# REPRODUCIBLE y SIN fuente externa: el repo no versiona los WAV originales
# (la media no entra en git), así que aquí se sintetizan ondas equivalentes en
# formato y tasa. Basta para compilar y ejercitar el mixer; el sonido exacto no
# se versiona (ver `tools/audio/README.md`).
#
# Salida: out/assets/audio/*.raw (gitignored).
#
# Uso: tools/audio/gen-demo-assets.sh
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="$ROOT/out/assets/audio"
GW="$ROOT/dist/tools/audio/gen-wave.js"

if [ ! -f "$GW" ]; then
	echo "falta $GW; compila las tools: npm run build" >&2
	exit 2
fi

mkdir -p "$OUT"
# nombre      tipo    Hz    rate   segundos   (parámetros por demos del mixer)
node "$GW" sine 440 44100 1.0 "$OUT/alien"          # 072 (≈44.3 kHz, 1 voz)
node "$GW" sine 440 11025 1.0 "$OUT/alien_11k"      # 073 (11025 Hz)
node "$GW" sine 80 11025 0.25 "$OUT/kick_mix"       # 074, 076 (percusión)
node "$GW" square 320 11025 0.2 "$OUT/snare_mix"    # 074, 076
node "$GW" square 6000 11025 0.1 "$OUT/hihat_mix"   # 074, 076
node "$GW" square 1800 11025 0.15 "$OUT/claps_mix"  # 074, 076

echo "OK: 6 .raw sintéticos en $OUT"
