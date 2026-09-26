#!/usr/bin/env node
// ============================================================================
// profile-diff: compara dos perfiles de WinUAE (`.bin` de `winuae-profile.mjs`) y
// resume el **delta de DMA por tipo** y de ciclos CPU ocupada/libre.
//
// Uso:
//   node tools/debug/winuae-profile.mjs <demo> A500_debug 20   # -> out/tmp/wprof-<demo>-*.bin
//   node tools/debug/profile-diff.mjs <A.bin> <B.bin>
//
// Por qué: comparar una version "lenta" contra su referencia (p. ej. una demo contra el
// original) localiza si el frame lo consume la CPU, el Blitter o el bus, y en qué cantidad.
// Los tipos de DMA son los `DMARECORD_*` del emulador (1=refresh, 2=CPU, 3=copper,
// 4=audio, 5=blitter, 6=bitplane, 7=sprite, 8=disk).
// ============================================================================
import fs from 'fs';
import { parseProfile } from '../../../mcp-winuae-emu/dist/profile-parse.js';

const DMA_NAMES = {
  1: 'refresh', 2: 'CPU', 3: 'copper', 4: 'audio', 5: 'blitter',
  6: 'bitplane', 7: 'sprite', 8: 'disk', 9: 'uhr_bpl', 10: 'uhr_spr', 11: 'conflict',
};

function summarize(file) {
  const p = parseProfile(fs.readFileSync(file));
  const n = Math.max(1, p.frames.length);
  let busy = 0, idle = 0;
  const dma = {};
  for (const f of p.frames) {
    busy += f.profileCycles || 0;
    idle += f.idleCycles || 0;
    for (const [k, v] of Object.entries(f.dmaSummary?.byType || {})) {
      dma[k] = (dma[k] || 0) + v;
    }
  }
  const avg = (o) => Object.fromEntries(Object.entries(o).map(([k, v]) => [k, Math.round(v / n)]));
  return { frames: p.frames.length, busy: Math.round(busy / n), idle: Math.round(idle / n), dma: avg(dma) };
}

const [a, b] = process.argv.slice(2);
if (!a || !b) {
  console.error('Uso: node tools/debug/profile-diff.mjs <A.bin> <B.bin>');
  process.exit(2);
}
const A = summarize(a), B = summarize(b);
console.log(`A = ${a}\nB = ${b}\n`);
console.log(`frames: A=${A.frames} B=${B.frames}`);
console.log(`CPU ocupada: A=${A.busy} B=${B.busy}  delta=${B.busy - A.busy}`);
console.log(`CPU libre  : A=${A.idle} B=${B.idle}  delta=${B.idle - A.idle}`);
console.log('\nDMA por frame (slots):');
const types = [...new Set([...Object.keys(A.dma), ...Object.keys(B.dma)])].sort((x, y) => x - y);
for (const t of types) {
  const va = A.dma[t] || 0, vb = B.dma[t] || 0;
  const name = DMA_NAMES[t] || `tipo${t}`;
  const d = vb - va;
  const tag = d === 0 ? '' : d > 0 ? `  (+${d})` : `  (${d})`;
  console.log(`  ${String(name).padEnd(9)} A=${String(va).padStart(7)} B=${String(vb).padStart(7)}${tag}`);
}
