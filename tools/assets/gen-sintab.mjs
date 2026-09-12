#!/usr/bin/env node
// Genera engine/include/eng/core/sintab.hpp (tabla de seno 4.12 EXACTA) a partir de
// la tabla del original demoscene: libmisc/sintab.c (que guarda DELTAS; la tabla se
// reconstruye sumando, como hace InitSinTab).
//
// Uso: node tools/assets/gen-sintab.mjs [--src <sintab.c>] [--out <sintab.hpp>]
//
// Por que existe: `sinetable.hpp` genera con Bhaskara+redondeo (difiere hasta +-8 de
// la tabla exacta del original). Los efectos portados 1:1 (p. ej. `plasma`) necesitan
// la tabla EXACTA; se materializa aqui una vez y la reutiliza `math2d::SinTableQ12`.
import * as fs from 'fs';

function arg(name, def) {
  const i = process.argv.indexOf(name);
  return i >= 0 ? process.argv[i + 1] : def;
}

const SRC = arg('--src', 'C:/Users/dvdjg/Documents/programa/AI/Amiga/demoscene-repo-orig/lib/libmisc/sintab.c');
const OUT = arg('--out', 'C:/Users/dvdjg/Documents/programa/AI/Amiga/Amiga-Cpp/engine/include/eng/core/sintab.hpp');

const text = fs.readFileSync(SRC, 'utf8');
const body = text.slice(text.indexOf('{') + 1, text.indexOf('};'));
const deltas = body.split(',').map((s) => s.trim()).filter((s) => s.length).map((s) => parseInt(s, 10));
if (deltas.length !== 4096) throw new Error('esperaba 4096 deltas, hay ' + deltas.length);

const tab = new Array(4096);
let sum = 0;
for (let i = 0; i < 4096; ++i) { sum += deltas[i]; tab[i] = sum; }

// Invariantes: sin(0)=0, sin(pi/2)=1, sin(pi)=0, sin(3pi/2)=-1.
for (const [i, v] of [[0, 0], [1024, 4096], [2048, 0], [3072, -4096]]) {
  if (tab[i] !== v) throw new Error('invariante i=' + i + ' = ' + tab[i] + ' != ' + v);
}

let out = '';
out += '#pragma once\n\n';
out += '/// \\file sintab.hpp\n';
out += '/// Tabla de seno 4.12 EXACTA del original (`libmisc/sintab.c`, 4096 pasos = 2 pi).\n';
out += '///\n';
out += '/// El original guarda los DELTAS y los suma en `InitSinTab`; aqui se materializa la\n';
out += '/// tabla ya sumada. `SinTableQ12` (math2d) y los efectos la reutilizan: es la fuente\n';
out += '/// unica y coincide **byte a byte** con la del demoscene (a diferencia de la\n';
out += '/// aproximacion de Bhaskara de `sinetable.hpp`, que difiere hasta +-8).\n';
out += '/// Generada por `tools/assets/gen-sintab.mjs`.\n';
out += '\n#include <eng/core/types.hpp>\n\n';
out += 'namespace eng {\n\n';
out += '/// `sin(i * 2pi / 4096) * 4096` truncado hacia cero (i en [0, 4096)).\n';
out += 'inline constexpr s16 kSinTab[4096] = {\n';
for (let i = 0; i < 4096; i += 16) {
  out += '\t' + tab.slice(i, i + 16).join(', ') + ',\n';
}
out += '};\n\n';
out += '} // namespace eng\n';
fs.writeFileSync(OUT, out, 'utf8');

let chk = 0;
for (let i = 0; i < 4096; ++i) chk = (chk * 31 + tab[i]) | 0;
console.log('OK: 4096 valores, checksum=' + chk + ' -> ' + OUT);
