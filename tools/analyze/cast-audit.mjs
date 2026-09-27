#!/usr/bin/env node
// Auditor de "smells" de tipo en el engine: cuenta `reinterpret_cast`, `const_cast`, `static_cast`
// y `void*` por fichero (reglas en CODING_STYLE §"Seguridad de tipos sobre punteros crudos").
//
//   node tools/analyze/cast-audit.mjs                 # tabla + totales (top por reinterpret_cast)
//   node tools/analyze/cast-audit.mjs --json          # salida compacta (para IA/diff)
//   node tools/analyze/cast-audit.mjs --check         # gate: falla si un fichero SUPERA su baseline
//   node tools/analyze/cast-audit.mjs --update-baseline  # (re)genera tools/check/casts-baseline.txt
//
// El baseline guarda, por fichero, el numero de `reinterpret_cast`+`const_cast` ACEPTADO (frontera
// real: registros custom, contenedores, ABI). El gate es de NO-REINCIDENCIA: no exige limpiar de
// golpe, pero impide que suba. Bajar el numero es libre; subirlo, no.

import * as fs from 'node:fs';
import * as path from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const BASELINE = path.join(ROOT, 'tools/check/casts-baseline.txt');
const DIRS = ['engine/include', 'engine/src'];
const ARGS = process.argv.slice(2);
const asJson = ARGS.includes('--json');
const check = ARGS.includes('--check');
const update = ARGS.includes('--update-baseline');

function walk(dir, acc) {
  for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
    const p = path.join(dir, e.name);
    if (e.isDirectory()) walk(p, acc);
    else if (/\.(hpp|cpp)$/.test(e.name)) acc.push(p);
  }
  return acc;
}

const files = [];
for (const d of DIRS) {
  const abs = path.join(ROOT, d);
  if (fs.existsSync(abs)) walk(abs, files);
}

const RE = {
  reinterpret_cast: /reinterpret_cast/g,
  const_cast: /const_cast/g,
  static_cast: /static_cast/g,
  'void*': /void\s*\*/g,
};
const count = (t, re) => (t.match(re) || []).length;
// Ignora comentarios: cuentan solo el CODIGO (una mencion en un comentario no es un cast).
const stripComments = (t) => t.replace(/\/\*[\s\S]*?\*\//g, ' ').replace(/\/\/[^\n]*/g, ' ');

const rows = files.map((f) => {
  const t = stripComments(fs.readFileSync(f, 'utf8'));
  const rel = path.relative(ROOT, f).replace(/\\/g, '/');
  const c = {};
  for (const [k, re] of Object.entries(RE)) c[k] = count(t, re);
  // "hard" = los que el baseline vigila (frontera declarada o deuda).
  c.hard = c.reinterpret_cast + c.const_cast;
  return { file: rel, ...c };
});
const totals = {};
for (const k of [...Object.keys(RE), 'hard']) totals[k] = rows.reduce((a, r) => a + r[k], 0);

if (update) {
  const lines = rows
    .filter((r) => r.hard > 0)
    .sort((a, b) => a.file.localeCompare(b.file))
    .map((r) => `${String(r.hard).padStart(3)}  ${r.file}`);
  fs.writeFileSync(
    BASELINE,
    `# Baseline de casts "duros" (reinterpret_cast + const_cast) por fichero.\n` +
      `# Regenerar solo tras JUSTIFICAR cada subida: node tools/analyze/cast-audit.mjs --update-baseline\n` +
      `# Formato: <n>  <ruta relativa>\n${lines.join('\n')}\n`,
  );
  console.log(`[casts] baseline -> ${path.relative(ROOT, BASELINE)} (${lines.length} ficheros)`);
  process.exit(0);
}

if (check) {
  const base = new Map();
  if (fs.existsSync(BASELINE)) {
    for (const line of fs.readFileSync(BASELINE, 'utf8').split(/\r?\n/)) {
      const m = line.match(/^\s*(\d+)\s+(.+?)\s*$/);
      if (m && !line.startsWith('#')) base.set(m[2], parseInt(m[1], 10));
    }
  }
  const fails = [];
  for (const r of rows) {
    const allowed = base.get(r.file) ?? 0;
    if (r.hard > allowed) {
      fails.push(`  ${r.file}: ${r.hard} > baseline ${allowed}`);
    }
  }
  if (fails.length) {
    console.error(`[casts] FALLO: casts "duros" por encima del baseline (${fails.length}):`);
    for (const f of fails) console.error(f);
    console.error('  Si el cast es frontera real, documentar y --update-baseline; si no, cambiar el tipo.');
    process.exit(1);
  }
  console.log(`[casts] OK: ningun fichero supera su baseline (total duros ${totals.hard}).`);
  process.exit(0);
}

if (asJson) {
  console.log(JSON.stringify({ totals, top: [...rows].sort((a, b) => b.hard - a.hard).slice(0, 25) }, null, 1));
} else {
  console.log(`[casts] ${files.length} ficheros | duros=${totals.hard} ` +
    `(reinterpret ${totals.reinterpret_cast} + const ${totals.const_cast}) | ` +
    `static=${totals.static_cast} | void*=${totals['void*']}`);
  console.log('  reinterpret+const  static  void*  fichero');
  for (const r of [...rows].sort((a, b) => b.hard - a.hard).slice(0, 20)) {
    console.log(
      `  ${String(r.hard).padStart(4)}             ${String(r.static_cast).padStart(5)}  ${String(r['void*']).padStart(4)}  ${r.file}`,
    );
  }
}
