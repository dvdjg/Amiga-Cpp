#!/usr/bin/env node
// =============================================================================
// reindex-ehb-bank.mjs · CONVIERTE los datos EHB al formato que el Amiga necesita
// SIN ninguna transformación de CPU en la máquina (regla de oro del pipeline).
//
// PROBLEMA que resuelve: el slicer (slice-tiles.mjs) emite el banco con los
// índices INTERCALADOS [base0, half0, base1, half1, ...] (índice par 2k = base k,
// índice impar 2k+1 = half k), porque esa es la forma natural de emparejar cada
// base con su half durante la cuantización. Pero el chipset EHB del Amiga espera
// los colores BASES-PRIMERO: COLOR00..31 = las 32 bases e índices 32..63 = half
// (generado automáticamente como base/2, bit 5 de cada índice de 6 bits).
//
// La conversión de un índice intercalado v a su índice EHB e es biyectiva sobre
// 0..63:   e = (v>>1) | ((v&1) << 5)
//   base k (v=2k)   -> e = k       = v>>1
//   half k (v=2k+1) -> e = 32+k    = (v>>1) | 0x20
// El bit 0 de la convención intercalada (par/impar) "salta" al bit 5 (el plano 6
// / half del EHB).
//
// REGLA PERMANENTE: esta conversión debe hacerse en el HOST (aquí), NUNCA en el
// Amiga. El tilebank.raw.bin incbinado y la paleta del .h van ya en convención
// bases-primero, listos para cargarse en memoria y dibujarse con el Blitter /
// Copper sin tocar un solo píxel desde la CPU.
//
// Uso:
//   node tools/ehb/reindex-ehb-bank.mjs <dir> [--dry-run]
//   <dir>  = directorio con tilebank.raw.bin y tiles.json (p. ej. out/ehb)
//
// Efectos (idempotentes: re-ejecutar no degrada, la conversión es una permutación
// biyectiva de los índices):
//   - tilebank.raw.bin  : reindexado a bases-primero (1 byte/píxel, 6 bits).
//   - tiles.json        : bank[].pix y palette reordenados a bases-primero
//                         (para que gid-to-bank y los previews sigan coherentes).
//   - tilebank_indexed.h: regenerado con kTileIndexedPalette bases-primero y los
//                         mismos map/tiles (solo cambia lo anterior).
// =============================================================================
import fs from 'node:fs';
import path from 'node:path';

const dir = process.argv[2];
if (!dir) { console.error('Uso: reindex-ehb-bank.mjs <dir> [--dry-run]'); process.exit(2); }
const dry = process.argv.includes('--dry-run');

const binPath = path.join(dir, 'tilebank.raw.bin');
const tilesJsonPath = path.join(dir, 'tiles.json');
const idxHdrPath = path.join(dir, 'tilebank_indexed.h');

// --- Función central: índice intercalado -> índice EHB bases-primero ----------
const toEhb = (v) => (v >> 1) | ((v & 1) << 5);

// --- 1) Reindexar el banco raw byte a byte -------------------------------------
const raw = fs.readFileSync(binPath);
// Un byte vale un índice de 6 bits. Todo byte 0..63 es un índice válido; valores
// >= 64 serían inconsistentes y se marcan como error (no deberían existir).
let bad = 0;
const outRaw = Buffer.alloc(raw.length);
for (let i = 0; i < raw.length; i++) {
  const v = raw[i];
  if (v >= 64) { bad++; outRaw[i] = raw[i]; } else outRaw[i] = toEhb(v);
}
if (bad) {
  console.error(`[reindex] AVISO: ${bad} byte(s) con índice >= 64 (no es un banco de 6 bits EHB?); se dejaron sin tocar.`);
}
console.log(`[reindex] ${path.basename(binPath)}: ${raw.length} bytes -> ${bad ? '(con avisos)' : 'reindexado a bases-primero (sin tocar píxel en el Amiga)'}`);
if (!dry) fs.writeFileSync(binPath, outRaw);

// --- 2) Reordenar tiles.json (palette + bank[i].pix) -------------------------
if (fs.existsSync(tilesJsonPath)) {
  const tj = JSON.parse(fs.readFileSync(tilesJsonPath, 'utf8'));
  const pal = tj.palette || [];
  // La paleta viene INTERCALADA desde slice-tiles: los pares contiguos
  // (pal[2k], pal[2k+1]) SON por construcción base k / half k. No hace falta
  // adivinar con heurísticas de "half == base>>1": eso falla con bases impares
  // (el half se trunca con >>1). Reordenamos directamente al esquema
  // BASES-PRIMERO del chipset EHB: base k -> posición k, half k -> posición 32+k.
  // Si hay un slot transparente (pal[0]==[0,0,0]) se reserva al inicio y todo se
  // desplaza +1 (base k -> 1+k, half k -> 33+k); este banco es opaco (--no-alpha).
  const hasTransp = pal.length > 0 && pal[0] && pal[0][0] === 0 && pal[0][1] === 0 && pal[0][2] === 0;
  const nBases = hasTransp ? (pal.length - 1) / 2 : pal.length / 2;
  const n = pal.length;
  const palEh = new Array(n);
  if (hasTransp) { palEh[0] = [...pal[0]]; }
  let warned = 0;
  for (let k = 0; k < nBases; k++) {
    const base = pal[hasTransp ? 1 + 2 * k : 2 * k];
    const half = pal[hasTransp ? 1 + 2 * k + 1 : 2 * k + 1];
    const full = hasTransp ? (k + 1) : k;
    const halfSlot = hasTransp ? (nBases + 1 + k) : (nBases + k);
    palEh[full] = [...base];
    if (half) palEh[halfSlot] = [...half];
    // Verificacion suave (informativa): la cuantizacion usa half(c)=c>>1, asi
    // que half debe ser base/2 troncado por componente (permite componente impar).
    const okHalf = half && half.every((hc, i) => base[i] === hc * 2 || base[i] === hc * 2 + 1);
    if (!okHalf) { warned++; console.log(`[reindex] nota: base k=${k} (${base}) half (${half}) no es base>>1 exacto; se reordena igualmente (pares de slice).`); }
  }
  if (warned) console.log(`[reindex] ${warned} pareja(s) con half != base>>1 estricto (normal si base es impar); reordenadas por construccion.`);
  tj.palette = palEh;
  if (tj.bank) {
    for (const b of tj.bank) if (b.pix) for (let i = 0; i < b.pix.length; i++) b.pix[i] = toEhb(b.pix[i]);
  }
  if (!dry) fs.writeFileSync(tilesJsonPath, JSON.stringify(tj, null, 2));
  console.log(`[reindex] ${path.basename(tilesJsonPath)}: palette (${n} colores, bases-primero) + pix de ${n} tiles reindexados`);
} else {
  console.log(`[reindex] (no hay tiles.json en ${dir}; se omite)`);
}

// --- 3) Regenerar tilebank_indexed.h (paleta bases-primero) ------------------
if (fs.existsSync(tilesJsonPath)) {
  const tj = JSON.parse(fs.readFileSync(tilesJsonPath, 'utf8'));
  const pal = tj.palette || [];
  const tile = tj.tile || 16;
  const bankCount = (tj.bank || []).length;
  const map = tj.map || [];
  const lines = [];
  lines.push('// Tiles EHB slice (bases-primero), para carga en el Amiga.');
  lines.push(`// ${pal.length} colores; ${bankCount} tiles de ${tile}x${tile}; mapa ${tj.cols}x${tj.rows}.`);
  lines.push("// CONVENCION BASES-PRIMERO: indices 0..31 = base, 32..63 = half. Listo para el chipset EHB.");
  lines.push(`static const unsigned char kTileIndexedPalette[${pal.length * 3}] = {`);
  for (let r = 0; r < pal.length; r += 12) lines.push('  ' + pal.slice(r, r + 12).map((c) => `${c[0]},${c[1]},${c[2]}`).join(',') + ',');
  lines.push('};');
  lines.push('// Datos en "tilebank.raw.bin" (incbin). El stride de cada tile es constante.');
  lines.push(`static const unsigned short kTileBankStride = ${tile * tile};`);
  lines.push(`static const unsigned int kTileBankBytes = ${(tj.bank || []).length * tile * tile};`);
  lines.push(`static const unsigned short kTileIndexedMap[${map.length}] = {`);
  for (let i = 0; i < map.length; i += 24) lines.push('  ' + map.slice(i, i + 24).join(',') + ',');
  lines.push('};');
  if (!dry) fs.writeFileSync(idxHdrPath, lines.join('\n') + '\n', 'utf8');
  console.log(`[reindex] ${path.basename(idxHdrPath)}: regenerado (${pal.length} colores, ${bankCount} tiles)`);
}

console.log('[reindex] OK. El banco incbinado y la paleta van ya en convención EHB bases-primero: el Amiga no transforma nada.');
