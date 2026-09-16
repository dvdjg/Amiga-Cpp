#!/usr/bin/env node
// Fuente UNICA de la tabla "funcion x escalar" de la libreria de matematicas.
//
// Genera (o comprueba) la tabla que vive en SCALAR_LIBRARY.md entre los marcadores
// <!-- SCALAR-TABLE:START --> / END, de modo que la doc no se desincronice de lo que los
// tests host/codegen prueban. Cada fila cita el test que la respalda.
//
//   node tools/check/scalar-support.mjs            # comprueba (falla si difiere)
//   node tools/check/scalar-support.mjs --write    # reescribe la tabla en la doc
import fs from 'node:fs';
import path from 'node:path';

const ROOT = path.resolve(
  path.dirname(new URL(import.meta.url).pathname.replace(/^\/([A-Za-z]:)/, '$1')),
  '../..',
);
const DOC = `${ROOT}/docs/engine/architecture/SCALAR_LIBRARY.md`;
const START = '<!-- SCALAR-TABLE:START -->';
const END = '<!-- SCALAR-TABLE:END -->';

// `si` / `no` / nota corta. El "test" cita quien lo prueba (host 059/060/057, codegen,
// math-diagnostics).
const ROWS = [
  { fn: 'clamp / saturate / step',   flt: 'si', mf: 'si', fx: 'si', test: 'HOST-059' },
  { fn: 'lerp',                      flt: 'si', mf: 'si (pierde incremento si |b-a| < |a|/2048)', fx: 'si', test: 'HOST-059' },
  { fn: 'smoothstep',                flt: 'si', mf: 'si', fx: 'si', test: 'HOST-059' },
  { fn: 'smootherstep',              flt: 'si', mf: 'si', fx: 'no (coef 15 > rango ±8)', test: 'math-diag smootherstep_q12_range' },
  { fn: 'inv_lerp / remap',          flt: 'si', mf: 'si', fx: 'si (div_norm)', test: 'HOST-059' },
  { fn: 'dot fusionado (2-4 pares)', flt: 'si', mf: 'si', fx: 'si (acumulador saturado)', test: 'HOST-059' },
  { fn: 'cross2 / rotate2 / vscale / vlerp', flt: 'si', mf: 'si', fx: 'si', test: 'HOST-059' },
  { fn: 'length / normalize / reflect / project', flt: 'si', mf: 'si (limites de rango)', fx: 'no (sin sqrt)', test: 'HOST-059' },
  { fn: 'value_noise / fbm',         flt: 'si', mf: 'si (coord <= 2048)', fx: 'no (necesita division)', test: 'HOST-060' },
  { fn: 'mul_add / mac (FMA)',       flt: '—', mf: 'si (1 redondeo)', fx: 'si (1 redondeo)', test: 'HOST-057/059' },
  { fn: 'wrap_angle / angle_diff',   flt: '—', mf: 'si', fx: '—', test: 'HOST-057' },
  { fn: 'sqrt/exp/log/sin/cos/tan',  flt: '—', mf: 'si', fx: '—', test: 'HOST-057' },
  { fn: 'transform (MF × fix)',      flt: '—', mf: 'ratio MF (|m| <= 8)', fx: 'coordenada', test: 'HOST-058' },
];

function renderTable() {
  const lines = [
    '| Función | `float`/`double` | `MiniFloat16` | `Fixed` 4.12 | Test que lo respalda |',
    '|---|---|---|---|---|',
  ];
  for (const r of ROWS) {
    lines.push(`| ${r.fn} | ${r.flt} | ${r.mf} | ${r.fx} | ${r.test} |`);
  }
  return lines.join('\n');
}

const doc = fs.readFileSync(DOC, 'utf8');
const table = renderTable();

if (process.argv.includes('--write')) {
  const i = doc.indexOf(START);
  const j = doc.indexOf(END);
  if (i < 0 || j < 0 || j < i) {
    console.error(`[scalar-support] la doc no tiene los marcadores ${START} / ${END}`);
    process.exit(1);
  }
  const next = doc.slice(0, i + START.length) + '\n\n' + table + '\n\n' + doc.slice(j);
  fs.writeFileSync(DOC, next);
  console.log('[scalar-support] tabla reescrita en SCALAR_LIBRARY.md');
  process.exit(0);
}

const i = doc.indexOf(START);
const j = doc.indexOf(END);
if (i < 0 || j < 0) {
  console.error(`[scalar-support] FAIL: faltan los marcadores en SCALAR_LIBRARY.md`);
  process.exit(1);
}
const current = doc.slice(i + START.length, j).replace(/^\s+|\s+$/g, '');
if (current !== table) {
  console.error('[scalar-support] FAIL: la tabla de SCALAR_LIBRARY.md no coincide con la fuente.');
  console.error('                  ejecuta: node tools/check/scalar-support.mjs --write');
  process.exit(1);
}
console.log('[scalar-support] OK: tabla funcion x escalar sincronizada con la fuente.');
