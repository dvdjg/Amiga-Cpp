#!/usr/bin/env node
// Gate de coste cero en el camino de frame (`ZERO_COST_FRAME_PATH.md`).
//
// Falla si en los ficheros del camino caliente aparece una **construcción local de `BlitJob`
// value-inicializada** (`BlitJob x {}` / `BlitJob x = {}`), que en `-O0` es un `memset` por objeto
// y en release una copia/`memcpy`, y que fue la causa del sobrecoste de CPU por BOB de la 213.
//
// El camino correcto es la construcción **in situ**:
//   FramePlan::begin_blit_job(kind)  ->  rellenar campos  ->  commit_blit_job()
//
// Uso: node tools/check/frame-hot-path.mjs
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');

// Ficheros del camino de frame (dibujo de objetos / limpieza). No incluye el setup de la escena.
const HOT_FILES = [
  'engine/include/eng/graphics/bob.hpp',
  'engine/include/eng/scene/bobs.hpp',
];

// `BlitJob <ident> {}` o `BlitJob <ident> = {}` a nivel de declaracion (no tipo de parametro).
const RE_LOCAL = /\bBlitJob\s+[A-Za-z_]\w*\s*(?:=\s*)?\{\}/;
// Copia local por asignacion de un BlitJob a otro (`BlitJob x = y;`), tambien costosa.
const RE_COPY = /\bBlitJob\s+[A-Za-z_]\w*\s*=\s*[A-Za-z_]\w*\s*;/;

let failures = 0;
for (const rel of HOT_FILES) {
  const file = path.join(ROOT, rel);
  if (!fs.existsSync(file)) continue;
  const lines = fs.readFileSync(file, 'utf8').split(/\r?\n/g);
  lines.forEach((line, i) => {
    const code = line.replace(/\/\/.*$/, '');
    if (RE_LOCAL.test(code) || RE_COPY.test(code)) {
      const isTypeParam = /\(/.test(code) && code.indexOf('BlitJob') > code.indexOf('(');
      if (!isTypeParam) {
        console.error(`[frame-hot-path] ${rel}:${i + 1}: construccion/copia local de BlitJob (usa begin_blit_job/commit_blit_job): ${line.trim()}`);
        ++failures;
      }
    }
  });
}

if (failures > 0) {
  console.error(`[frame-hot-path] ${failures} incumplimiento(s) de coste cero en el camino de frame.`);
  process.exit(1);
}
console.log('[frame-hot-path] OK: sin construccion/copia local de BlitJob en el camino de frame.');
