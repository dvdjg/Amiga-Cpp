#!/usr/bin/env node

// Evalúa la vertical OGG/WAV -> modelos armónicos -> ACP1 v3 -> player host.
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';

const root = process.cwd();
const executable = path.join(root, 'out', 'tmp', 'audio-compressor', 'audio-compressor.exe');
const fallbackExecutable = path.join(root, 'out', 'tmp', 'audio-compressor', 'audio-compressor');
const outputDir = path.join(root, 'out', 'playground', 'audio-compressor', 'synth-separation');
const inputs = process.argv.slice(2);
if (inputs.length === 0) {
  console.error('Uso: node tools/bench-synth-separation.mjs <pieza.ogg> [<pieza2.ogg> ...]');
  process.exit(2);
}
const binary = fs.existsSync(executable) ? executable : fallbackExecutable;
if (!fs.existsSync(binary)) {
  console.error('No existe el ejecutable. Compile con: bash host-tools/audio-compressor/build.sh');
  process.exit(2);
}
fs.mkdirSync(outputDir, { recursive: true });
const rows = [];
for (const input of inputs) {
  const absolute = path.resolve(input);
  const stem = path.basename(absolute).replace(/\.[^.]+$/, '').replace(/[^A-Za-z0-9_-]+/g, '_');
  const output = path.join(outputDir, `${stem}.acp1`);
  const result = spawnSync(binary, [absolute, '--synth-separate', '--force', '--out', output], { encoding: 'utf8' });
  if (result.status !== 0) {
    console.error(`${path.basename(input)}: fallo\n${result.stdout ?? ''}${result.stderr ?? ''}`);
    process.exit(result.status || 1);
  }
  const match = `${result.stdout ?? ''}${result.stderr ?? ''}`.match(/modelos=(\d+) bytes=(\d+) MSE=([0-9.]+) SNR=([-0-9.]+) pico=(\d+) fuga_dB=([-0-9.]+)/);
  rows.push({ name: path.basename(input), inputBytes: fs.statSync(absolute).size, outputBytes: fs.statSync(output).size, models: match?.[1] ?? '?', mse: match?.[3] ?? '?', snr: match?.[4] ?? '?', leakage: match?.[6] ?? '?' });
}
console.log('pieza | entrada bytes | ACP1 v3 bytes | modelos | MSE PCM8 | SNR dB | fuga dB');
console.log('--- | ---: | ---: | ---: | ---: | ---: | ---:');
for (const row of rows) console.log(`${row.name} | ${row.inputBytes} | ${row.outputBytes} | ${row.models} | ${row.mse} | ${row.snr} | ${row.leakage}`);
