// Detector de regresiones de nivel del motor de naipes (`eng::cards`).
//
// Compara el CSV de un barrido de `tools/cards/selfplay.sh --compare --sweep --csv`
// con la linea base congelada `tools/cards/regression-baseline.csv`. Como la
// simulacion es determinista (misma semilla, aritmetica entera), cualquier cambio en
// reglas, equity o IA altera los numeros y hace fallar el gate: obliga a revisar si
// la regresion es intencionada y, si lo es, a regenerar la base.
//
// Uso:
//   node tools/cards/check-regression.mjs <candidato.csv> [--update]
//
// `--update` copia el candidato sobre la base (solo cuando la regresion es buscada).

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const BASELINE = path.join(__dirname, 'regression-baseline.csv');

const args = process.argv.slice(2);
const update = args.includes('--update');
const candidate = args.find((a) => !a.startsWith('--'));
if (!candidate) {
  console.error('Uso: node tools/cards/check-regression.mjs <candidato.csv> [--update]');
  process.exit(2);
}

function parseCsv(text) {
  const rows = [];
  for (const line of text.split(/\r?\n/)) {
    if (!line || line.startsWith('mode,')) continue;
    const cols = line.split(',');
    if (cols.length < 11) continue;
    const key = `${cols[0]}|${cols[1]}|${cols[2]}`;
    rows.push({
      key,
      mode: cols[0],
      profile: cols[1],
      seat: Number(cols[2]),
      style: cols[3],
      net: Number(cols[4]),
      bb100: Number(cols[5]),
      hands: Number(cols[6]),
      showdowns: Number(cols[7]),
      raises: Number(cols[8]),
      calls: Number(cols[9]),
      folds: Number(cols[10]),
    });
  }
  return rows;
}

const candText = fs.readFileSync(candidate, 'utf8');
const cand = parseCsv(candText);

if (update) {
  fs.writeFileSync(BASELINE, candText, 'utf8');
  console.log(`[cards-regression] base actualizada: ${BASELINE} (${cand.length} filas)`);
  process.exit(0);
}

if (!fs.existsSync(BASELINE)) {
  console.error(`[cards-regression] FAIL: no existe la base ${BASELINE}`);
  process.exit(1);
}
const base = parseCsv(fs.readFileSync(BASELINE, 'utf8'));

const problems = [];
const baseByKey = new Map(base.map((r) => [r.key, r]));
const candByKey = new Map(cand.map((r) => [r.key, r]));

for (const [key, b] of baseByKey) {
  const c = candByKey.get(key);
  if (!c) {
    problems.push(`falta la fila ${key}`);
    continue;
  }
  for (const field of ['net', 'bb100', 'hands', 'showdowns', 'raises', 'calls', 'folds']) {
    if (c[field] !== b[field]) {
      problems.push(`${key}: ${field} ${b[field]} -> ${c[field]}`);
    }
  }
}
for (const key of candByKey.keys()) {
  if (!baseByKey.has(key)) problems.push(`fila nueva ${key}`);
}

// Invariantes independientes de la base: el barrido debe haber jugado manos.
for (const row of cand) {
  if (row.hands <= 0) problems.push(`${row.key}: hands=${row.hands}`);
  if (row.raises + row.calls + row.folds <= 0) problems.push(`${row.key}: sin acciones`);
}

if (problems.length) {
  for (const p of problems) console.error(`[cards-regression] FAIL: ${p}`);
  console.error(`[cards-regression] ${problems.length} diferencia(s); si es intencionado: --update`);
  process.exit(1);
}
console.log(`[cards-regression] OK: ${cand.length} filas iguales a la base`);
