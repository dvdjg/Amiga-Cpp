#!/usr/bin/env node
// gen-melody.mjs: sintetiza una melodia **de dominio publico** (Oda a la Alegria, Beethoven) a
// PCM mono 8-bit con signo para las demos de streaming. No usa muestras externas: es una onda
// sintetizada, asi que no hay material con derechos que versionar.
//
// Uso:
//   node tools/audio/gen-melody.mjs <out.raw> [--rate 8000] [--gain 96]
//
// Salida: `<out.raw>` (1 byte/muestra, s8) y, por stdout, el numero de muestras y un checksum
// XOR (lo usa la demo 278 para el gate de "descomprimido == melodia").
import fs from 'node:fs';

const args = process.argv.slice(2);
const out = args[0];
const rate = Number(args[args.indexOf('--rate') + 1]) || 8000;
const gain = Number(args[args.indexOf('--gain') + 1]) || 96;
if (!out) {
  console.error('uso: node tools/audio/gen-melody.mjs <out.raw> [--rate 8000] [--gain 96]');
  process.exit(2);
}

// Frecuencias (Hz) de las notas usadas por el tema (afinacion estandar A4=440).
const F = {
  C4: 261.63, D4: 293.66, E4: 329.63, F4: 349.23, G4: 392.0, A4: 440.0, B4: 493.88,
  C5: 523.25, D5: 587.33, G3: 196.0, A3: 220.0, B3: 246.94, F3: 174.61,
};

// Oda a la Alegria (tema principal): {nota, duracion en corcheas}. null = silencio.
const S = 0.18; // segundos por corchea
const MELODY = [
  ['E4', 1], ['E4', 1], ['F4', 1], ['G4', 1],
  ['G4', 1], ['F4', 1], ['E4', 1], ['D4', 1],
  ['C4', 1], ['C4', 1], ['D4', 1], ['E4', 1],
  ['E4', 1.5], ['D4', 0.5], ['D4', 2],
  ['E4', 1], ['E4', 1], ['F4', 1], ['G4', 1],
  ['G4', 1], ['F4', 1], ['E4', 1], ['D4', 1],
  ['C4', 1], ['C4', 1], ['D4', 1], ['E4', 1],
  ['D4', 1.5], ['C4', 0.5], ['C4', 2],
];

const samples = [];
for (const [name, beats] of MELODY) {
  const f = F[name];
  const n = Math.round(beats * S * rate);
  const attack = Math.min(n >> 4, rate >> 6);
  const release = Math.min(n >> 3, rate >> 5);
  for (let i = 0; i < n; i++) {
    let env = 1;
    if (i < attack) env = i / attack;
    else if (i >= n - release) env = (n - i) / release;
    const v = Math.sin((2 * Math.PI * f * i) / rate) * gain * env;
    samples.push(Math.max(-128, Math.min(127, Math.round(v))) & 0xff);
  }
}
// Pequeno silencio final para que el ultimo chunk no quede a medias.
for (let i = 0; i < Math.round(0.2 * rate); i++) samples.push(0);

const buf = Buffer.from(samples);
fs.writeFileSync(out, buf);
let chk = 0;
for (const b of buf) chk ^= b;
console.log(`gen-melody: ${buf.length} muestras (${rate} Hz) -> ${out}  checksum=0x${(chk >>> 0).toString(16)}`);
