// Test de regresión de la permutación de export EHB (tools/ehb/ehb-export-map.mjs).
// Valida que `buildExport` reindexa la paleta/índices INTERCALADOS a BASES-PRIMERO
// (regla 7) de forma biyectiva y correcta, tanto sin slot transparente (T=0, EHB
// opaco) como con slot transparente (T=1). Sale con código !=0 si falla.
//
// Uso: node tools/ehb/test-expindex.mjs
import { buildExport } from './ehb-export-map.mjs';

let failures = 0;
const ok = (name, cond) => { if (!cond) { failures++; console.error(`FAIL: ${name}`); } else { console.log(`ok: ${name}`); } };

function checkCase(T, nB, label) {
  const palSize = T + 2 * nB;
  // paletteI intercalada: [transparente?] [base0,half0,base1,half1,...]
  const paletteI = [];
  if (T) paletteI.push([0, 0, 0]);
  for (let k = 0; k < nB; k++) {
    const b = [100 + k, 20 + k, 5];       // base k
    const h = [b[0] >> 1, b[1] >> 1, b[2] >> 1]; // half k
    paletteI.push(b, h);
  }
  const { expPalette, expIndex } = buildExport(paletteI, T === 1);

  // 1) biyectiva sobre 0..palSize-1
  const seen = new Set(); let bijective = true;
  for (let v = 0; v < palSize; v++) { const e = expIndex(v); if (e < 0 || e >= palSize || seen.has(e)) bijective = false; seen.add(e); }
  ok(`${label}: expIndex biyectiva 0..${palSize - 1}`, bijective && seen.size === palSize);

  // 2) colocación de bases/halves en expPalette
  let place = true;
  for (let k = 0; k < nB; k++) {
    const bp = expPalette[T + k][0];        // base k -> pos T+k
    const hp = expPalette[T + nB + k][0];   // half k -> pos T+nB+k
    if (bp !== 100 + k) place = false;
    if (hp !== (100 + k) >> 1) place = false;
  }
  ok(`${label}: bases en ${T}..${T + nB - 1}, halves en ${T + nB}..${palSize - 1}`, place);

  // 3) expIndex mapea base k intercalado (v=T+2k) -> T+k, half k (v=T+2k+1) -> T+nB+k
  let mapOk = true;
  for (let k = 0; k < nB; k++) {
    if (expIndex(T + 2 * k) !== T + k) mapOk = false;
    if (expIndex(T + 2 * k + 1) !== T + nB + k) mapOk = false;
  }
  ok(`${label}: base v->pos ${T}+k, half v->pos ${T + nB}+k`, mapOk);

  // 4) transparente (si T) se queda en 0
  if (T) ok(`${label}: transparente se queda en ${0}`, expIndex(0) === 0);

  // 5) NOTA: expIndex es biyectiva (1:1) pero NO idempotente — aplicarla dos veces
  // sobre datos ya reindexados degradaría. Solo se aplica una vez a los índices
  // intercalados internos en el export; no se re-ejecuta sobre datos bases-primero.

  // 6) equivale a la fórmula canónica e=(u>>1)|((u&1)<<5) tras restar el slot
  for (let vsee = T; vsee < palSize; vsee++) {
    const u = vsee - T;
    const ref = (u >> 1) | ((u & 1) << 5);
    if (ref >= nB && nB < 32) { /* 5 bits: la fórmula asume 6 bits; solo aplica a nB=32 */ }
    if (nB === 32 && expIndex(vsee) !== T + ref) { ok(`${label}: fórmula ref en v=${vsee}`, false); }
  }
  if (nB !== 32) console.log(`  (${label}: salto la comparación con fórmula 6 bits, nB=${nB})`);
}

checkCase(0, 32, 'EHB opaco T=0');
checkCase(1, 32, 'EHB transparente T=1');
if (failures === 0) { console.log('TODOS OK'); process.exit(0); } else { console.error(`${failures} fallo(s)`); process.exit(1); }
