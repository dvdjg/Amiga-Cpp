#!/usr/bin/env node
// ---------------------------------------------------------------------------
// **Cadencia por parejas de frames**: extrae los screenshots por frame de un
// perfil WinUAE (`monitor profile N "" bin`) y detecta si frames CONSECUTIVOS
// son IDENTICOS (firma FNV sobre el buffer). Sirve para medir la cadencia de un
// binario SIN simbologia (p. ej. una demo original de referencia en ADF):
//
//   - todos los frames distintos  -> la imagen avanza cada campo (50 Hz)
//   - parejas identicas (0=1, 2=3, ...) -> avanza 1 vez cada 2 campos = 25 fps
//
// El perfil se captura con `tools/debug/winuae-profile.mjs` (o el MCP) sobre la
// config del binario; ver `docs/tools/PROFILING_FROM_AGENT.md` §«Cadencia de un
// binario de referencia».
//
// Uso:
//   node tools/debug/profile-cadence.mjs <perfil.bin> [dirSalida] [prefijo]
//   - dirSalida por defecto: out/tmp/<perfil>-frames/
// ---------------------------------------------------------------------------
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { parseProfile } from '../../../mcp-winuae-emu/dist/profile-parse.js';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const bin = process.argv[2];
if (!bin) {
  console.error('Uso: node tools/debug/profile-cadence.mjs <perfil.bin> [dirSalida] [prefijo]');
  process.exit(2);
}
const prefix = process.argv[4] || 'f';
const outDir = process.argv[3] || path.join(ROOT, 'out', 'tmp', `${path.basename(bin, '.bin')}-frames`);
fs.mkdirSync(outDir, { recursive: true });

const parsed = parseProfile(fs.readFileSync(bin));
console.log(`frames=${parsed.frames.length} baseClock=${parsed.baseClock}`);
const sig = [];
parsed.frames.forEach((f, i) => {
  const buf = f.screenshot;
  if (!buf || buf.length === 0) {
    console.log(`frame ${i}: sin screenshot`);
    return;
  }
  const ext = f.screenshotType === 'png' ? 'png' : 'jpg';
  const file = path.join(outDir, `${prefix}${String(i).padStart(2, '0')}.${ext}`);
  fs.writeFileSync(file, buf);
  // Firma FNV-1a barata sobre el buffer: igual firma = frame identico.
  let h = 0x811c9dc5;
  for (let k = 0; k < buf.length; k++) {
    h ^= buf[k];
    h = Math.imul(h, 0x01000193) >>> 0;
  }
  sig.push(h.toString(16));
  console.log(`frame ${i}: ${file} (${buf.length} B, firma ${h.toString(16)})`);
});
let same = 0;
for (let i = 1; i < sig.length; i++) {
  if (sig[i] === sig[i - 1]) {
    same++;
  }
}
console.log(`pares consecutivos identicos: ${same}/${Math.max(0, sig.length - 1)}`);
if (same > 0) {
  console.log('lectura: la imagen avanza 1 vez cada', (sig.length - 1) / Math.max(1, sig.length - 1 - same) + ' campos (aprox)');
}
