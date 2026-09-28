#!/usr/bin/env node
// Detecta CONSTANTES DUPLICADAS en el engine: el MISMO valor declarado bajo **nombres distintos
// en ficheros distintos** (p. ej. `kBlitterMintermCopyC` y `blt_minterm_copy_c`, ambos `0xaa`).
//
// No es un gate (hay valores legítimamente repetidos: 0, 1, máscaras 0xff/0xffff...). Es un
// **aviso** para revisar ANTES de añadir una constante nueva: si ya existe, reutilizarla.
//
//   node tools/analyze/duplicate-constants.mjs            # valores repetidos (>=2 nombres, >=2 ficheros)
//   node tools/analyze/duplicate-constants.mjs --min 16   # ignora valores por debajo de N
//
// Regla relacionada: CODING_STYLE (§«antes de crear, reutilizar») y §232.

import * as fs from 'node:fs';
import * as path from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const DIRS = ['engine/include', 'engine/src'];
const ARGS = process.argv.slice(2);
const minIdx = ARGS.indexOf('--min');
const MIN = minIdx >= 0 ? Number(ARGS[minIdx + 1]) : 64;
// Valores que se repiten a propósito (no son duplicados útiles).
const IGNORE = new Set([0xff, 0xffff, 0xfff, 0xff00, 0xffffffff]);
// Un valor es "distintivo" si no es potencia de dos (eso suele ser un TAMAÑO, no un duplicado) y
// supera el mínimo. Así se cazan los que importan (`0xAA`, `0xCA`, `0xF0`…), no los `32`/`0x100`.
const interesting = (v) => v >= MIN && (v & (v - 1)) !== 0 && !IGNORE.has(v);

function walk(dir, acc) {
  if (!fs.existsSync(dir)) return acc;
  for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
    const p = path.join(dir, e.name);
    if (e.isDirectory()) walk(p, acc);
    else if (/\.(hpp|cpp)$/.test(e.name)) acc.push(p);
  }
  return acc;
}

// `constexpr <tipo...> <NOMBRE> = <numero>;` (literales decimal/hex, con sufijo u/l opcional).
const RE = /constexpr\s+[^=;{}]*?\b([A-Za-z_]\w*)\s*=\s*(0x[0-9a-fA-F]+|\d+)[uUlL]*\s*;/g;
const byValue = new Map(); // valor -> [{ name, file }]
const files = [];
for (const d of DIRS) walk(path.join(ROOT, d), files);
for (const f of files) {
  const rel = path.relative(ROOT, f).replace(/\\/g, '/');
  const text = fs.readFileSync(f, 'utf8');
  for (const m of text.matchAll(RE)) {
    const name = m[1];
    const value = m[2].startsWith('0x') ? parseInt(m[2], 16) : parseInt(m[2], 10);
    if (!interesting(value)) continue;
    if (!byValue.has(value)) byValue.set(value, []);
    byValue.get(value).push({ name, file: rel });
  }
}

const dups = [...byValue.entries()].filter(([, hits]) => {
  const names = new Set(hits.map((h) => h.name));
  const fs_ = new Set(hits.map((h) => h.file));
  return names.size >= 2 && fs_.size >= 2;
});

if (dups.length === 0) {
  console.log(`[dup-consts] OK: sin valores repetidos bajo nombres distintos (>= ${MIN}).`);
  process.exit(0);
}
console.log(`[dup-consts] ${dups.length} valor(es) repetido(s) bajo nombres distintos:`);
for (const [value, hits] of dups.sort((a, b) => a[0] - b[0])) {
  console.log(`  0x${value.toString(16)} (${value}):`);
  for (const h of hits) console.log(`    ${h.name.padEnd(30)} ${h.file}`);
}
console.log('  Revisar: reutilizar la existente en vez de declarar otra (CODING_STYLE).');
