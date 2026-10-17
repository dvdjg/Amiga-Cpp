#!/usr/bin/env node
// ---------------------------------------------------------------------------
// **Lupa del HUD**: amplia la banda de texto de una captura (recorte + escala
// nearest) para leer los digitos con certeza (los modelos de vision confunden
// glifos como 3/4). Herramienta de diagnostico del HUD de depuracion
// (contador de frames / fps); ver `docs/tools/PROFILING_FROM_AGENT.md`.
//
// Uso:
//   node tools/debug/hud-zoom.mjs <captura.png> [salida.png] [x y w h] [escala]
//   - Por defecto recorta la banda superior de una captura PAL completa
//     ([100,30 600x45]) a escala 2: suficiente para las 079/116.
//
// Guarda la salida en `out/tmp/` si no se indica fichero.
// ---------------------------------------------------------------------------
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { PNG } from '../../node_modules/pngjs/lib/png.js';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const argv = process.argv.slice(2);
const inP = argv[0];
if (!inP) {
  console.error('Uso: node tools/debug/hud-zoom.mjs <captura.png> [salida.png] [x y w h] [escala]');
  process.exit(2);
}
const outP = argv[1] && !/^\d/.test(argv[1])
  ? argv[1]
  : path.join(ROOT, 'out', 'tmp', `hud-zoom-${path.basename(inP)}`);
const nums = argv.slice(argv[1] && !/^\d/.test(argv[1]) ? 2 : 1).map(Number);
const [x = 100, y = 30, w = 600, h = 45] = nums.slice(0, 4);
const scale = nums[4] || 2;

const src = PNG.sync.read(fs.readFileSync(inP));
const dst = new PNG({ width: w * scale, height: h * scale });
for (let yy = 0; yy < h * scale; yy++) {
  for (let xx = 0; xx < w * scale; xx++) {
    const sx = Math.min(src.width - 1, x + Math.floor(xx / scale));
    const sy = Math.min(src.height - 1, y + Math.floor(yy / scale));
    const si = (sy * src.width + sx) * 4;
    const di = (yy * dst.width + xx) * 4;
    dst.data[di] = src.data[si];
    dst.data[di + 1] = src.data[si + 1];
    dst.data[di + 2] = src.data[si + 2];
    dst.data[di + 3] = 255;
  }
}
fs.mkdirSync(path.dirname(outP), { recursive: true });
fs.writeFileSync(outP, PNG.sync.write(dst));
console.log(`zoom [${x},${y} ${w}x${h}] x${scale} -> ${outP} (origen ${src.width}x${src.height})`);
