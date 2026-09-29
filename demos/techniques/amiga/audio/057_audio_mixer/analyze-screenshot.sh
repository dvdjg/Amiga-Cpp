#!/usr/bin/env bash
# Analizador visual de la demo 057 (audio mixer): barra de tono cian sobre fondo
# solido. La demo es una prueba de audio; su visual es minimo (no un degradado):
# el fondo es el registro COLOR00 y la barra es COLOR01 (cian). No exige blanco.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../../.." && pwd)"
IMAGE="${1:-}"
if [ -z "$IMAGE" ]; then
	echo "Uso: analyze-screenshot.sh <imagen.png>" >&2
	exit 2
fi
ROOT="$ROOT" exec node -e '
const { readPng } = require(process.env.ROOT + "/dist/tools/lib/image.js");
const img = readPng(process.argv[1]);
let cyan = 0;
let red = 0;
for (let y = 0; y < img.height; y++) {
  for (let x = 0; x < img.width; x++) {
    const i = (y * img.width + x) * 4;
    const r = img.data[i], g = img.data[i + 1], b = img.data[i + 2];
    if (g > 150 && b > 150 && r < 80) cyan++;
    if (r > 150 && r >= g * 2 && r >= b * 2) red++;
  }
}
if (cyan < 20) { console.error("FAIL audio: barra de tono cian ausente (cyan=" + cyan + ")"); process.exit(1); }
if (red < 200) { console.error("FAIL audio: fondo (COLOR00) ausente (red=" + red + ")"); process.exit(1); }
console.log("OK audio: cian=" + cyan + " rojo=" + red);
' "$IMAGE"
