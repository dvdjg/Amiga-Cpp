#!/usr/bin/env node

// Compara el separador armónico actual con el diccionario espectral experimental.
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';

const root = process.cwd();
const binary = path.join(root, 'out', 'tmp', 'audio-compressor', process.platform === 'win32' ? 'audio-compressor.exe' : 'audio-compressor');
const inputs = process.argv.slice(2);
if (inputs.length === 0) { console.error('Uso: node tools/bench-separation-comparison.mjs <pieza.ogg> [<pieza2.ogg> ...]'); process.exit(2); }
if (!fs.existsSync(binary)) { console.error('No existe el ejecutable. Compile con: bash host-tools/audio-compressor/build.sh'); process.exit(2); }

function run(args) {
  const result = spawnSync(binary, args, { encoding: 'utf8', maxBuffer: 1024 * 1024 });
  if (result.status !== 0) throw new Error(`${args[0]}: ${result.stdout ?? ''}${result.stderr ?? ''}`);
  return `${result.stdout ?? ''}${result.stderr ?? ''}`;
}

const rows = [];
for (const input of inputs) {
  const absolute = path.resolve(input);
  const old = run([absolute, '--synth-separate', '--force', '--out', path.join(root, 'out', 'tmp', 'audio-compressor', 'comparison.acp1')]);
  const spectral = run([absolute, '--spectral-both']);
  const oldMatch = old.match(/MSE=([0-9.]+) SNR=([-0-9.]+).*fuga_dB=([-0-9.]+)/);
  const spectralMatches = [...spectral.matchAll(/max=(\d+) prototipos=(\d+) MSE_mag=([0-9.]+) SNR_mag=([-0-9.]+) residual=([0-9.]+)/g)];
  rows.push({
    name: path.basename(input),
    oldMse: oldMatch?.[1] ?? '?',
    oldSnr: oldMatch?.[2] ?? '?',
    oldLeakage: oldMatch?.[3] ?? '?',
    spectral3: spectralMatches.find((match) => match[1] === '3'),
    spectral8: spectralMatches.find((match) => match[1] === '8'),
  });
}

console.log('pieza | armónico MSE PCM | armónico SNR PCM | armónico fuga | espectral 3 SNR mag | espectral 3 residual | espectral 8 SNR mag | espectral 8 residual');
console.log('--- | ---: | ---: | ---: | ---: | ---: | ---: | ---:');
for (const row of rows) console.log(`${row.name} | ${row.oldMse} | ${row.oldSnr} | ${row.oldLeakage} | ${row.spectral3?.[4] ?? '?'} | ${row.spectral3?.[5] ?? '?'} | ${row.spectral8?.[4] ?? '?'} | ${row.spectral8?.[5] ?? '?'}`);
