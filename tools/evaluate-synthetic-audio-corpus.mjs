#!/usr/bin/env node

// Ejecuta la separación calibrada y compara cada pista recuperada con los stems conocidos.
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';

const root = process.cwd();
const corpus = path.resolve(process.argv[2] ?? 'out/playground/audio-compressor/synthetic-corpus');
const manifest = JSON.parse(fs.readFileSync(path.join(corpus, 'manifest.json'), 'utf8'));
const binary = path.join(root, 'out', 'tmp', 'audio-compressor', process.platform === 'win32' ? 'audio-compressor.exe' : 'audio-compressor');
const output = path.join(corpus, 'evaluation');
fs.mkdirSync(output, { recursive: true });

function wav(file) {
  const bytes = fs.readFileSync(file);
  const result = new Int16Array(bytes.length - 44);
  for (let i = 44; i < bytes.length; i += 1) result[i - 44] = bytes[i] - 128;
  return result;
}

function similarity(left, right) {
  const count = Math.min(left.length, right.length);
  let dot = 0; let leftEnergy = 0; let rightEnergy = 0; let error = 0;
  for (let i = 0; i < count; i += 1) { dot += left[i] * right[i]; leftEnergy += left[i] ** 2; rightEnergy += right[i] ** 2; error += (left[i] - right[i]) ** 2; }
  return { correlation: dot / Math.sqrt(Math.max(1, leftEnergy * rightEnergy)), mse: error / count };
}

function eventRecall(source, events) {
  const frame = 256;
  const energy = [];
  for (let start = 0; start < source.length; start += frame) {
    let value = 0;
    for (let i = start; i < Math.min(source.length, start + frame); i += 1) value += Math.abs(source[i]);
    energy.push(value / Math.min(frame, source.length - start));
  }
  const peak = Math.max(1, ...energy);
  let found = 0;
  for (const event of events) {
    const first = Math.max(0, Math.floor((event.start * manifest.rate) / frame) - 1);
    const last = Math.min(energy.length - 1, Math.ceil(((event.start + event.duration) * manifest.rate) / frame) + 1);
    if (energy.slice(first, last + 1).some((value) => value >= peak * 0.12)) found += 1;
  }
  return events.length === 0 ? 0 : found / events.length;
}

const args = [path.join(corpus, 'mix.wav'), '--spectral-both', '--spectral-calibration', path.join(corpus, 'manifest.json'), '--spectral-export-dir', output];
const run = spawnSync(binary, args, { encoding: 'utf8', maxBuffer: 1024 * 1024 });
if (run.status !== 0) { console.error(`${run.stdout ?? ''}${run.stderr ?? ''}`); process.exit(run.status || 1); }
const references = Object.fromEntries(manifest.references.map((file) => [file.replace(/^reference-|\.wav$/g, ''), wav(path.join(corpus, file))]));
console.log(`${run.stdout ?? ''}`.trim());
for (const variant of ['spectral-3', 'spectral-8']) {
  const directory = path.join(output, variant);
  const tracks = fs.readdirSync(directory).filter((file) => /^track-\d+\.wav$/.test(file)).sort();
  console.log(`variant=${variant}`);
  for (const track of tracks) {
    const source = wav(path.join(directory, track));
    const matches = Object.entries(references).map(([name, reference]) => ({ name, ...similarity(source, reference) })).sort((a, b) => b.correlation - a.correlation);
    const best = matches[0];
    console.log(`${track} -> ${best.name} correlation=${best.correlation.toFixed(4)} MSE=${best.mse.toFixed(2)} event_recall=${eventRecall(source, manifest.events[best.name] ?? []).toFixed(3)}`);
  }
}
