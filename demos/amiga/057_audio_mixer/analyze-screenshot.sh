#!/usr/bin/env bash
# Analizador visual de la demo 057 (audio mixer): degradado de fondo + barra de
# tono cian. No exige píxeles blancos: la barra es cian y el fondo un degradado.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
IMAGE="${1:-}"
if [ -z "$IMAGE" ]; then
	echo "Uso: analyze-screenshot.sh <imagen.png>" >&2
	exit 2
fi
ROOT="$ROOT" exec node -e '
const { readPng } = require(process.env.ROOT + "/dist/tools/lib/image.js");
const img = readPng(process.argv[1]);
let cyan = 0;
const tones = new Set();
for (let y = 0; y < img.height; y++) {
  for (let x = 0; x < img.width; x++) {
    const i = (y * img.width + x) * 4;
    const r = img.data[i], g = img.data[i + 1], b = img.data[i + 2];
    const max = Math.max(r, g, b);
    if (max < 64) continue;
    if (g > 150 && b > 150 && r < 80) cyan++;
    const t = 2;
    if (r >= g * t && r >= b * t) tones.add("red");
    else if (g >= r * t && g >= b * t) tones.add("green");
    else if (b >= r * t && b >= g * t) tones.add("blue");
  }
}
if (cyan < 20) { console.error("FAIL audio: barra de tono cian ausente (cyan=" + cyan + ")"); process.exit(1); }
if (tones.size < 3) { console.error("FAIL audio: degradado pobre (" + tones.size + " familias)"); process.exit(1); }
console.log("OK audio: cian=" + cyan + " familias=" + tones.size);
' "$IMAGE"
