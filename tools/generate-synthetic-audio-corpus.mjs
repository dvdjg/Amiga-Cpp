#!/usr/bin/env node

// Genera una mezcla determinista con instrumentos sintéticos, solos de calibración y stems de referencia.
import fs from 'node:fs';
import path from 'node:path';

const args = process.argv.slice(2);
const outArg = args.indexOf('--out');
const out = path.resolve(outArg >= 0 ? args[outArg + 1] : 'out/playground/audio-compressor/synthetic-corpus');
const rate = 11025;
const seconds = 12;
const samples = rate * seconds;
const pi2 = Math.PI * 2;

const instruments = {
  bass: { base: 110, harmonics: [1, 0.55, 0.22, 0.1], attack: 0.02, release: 0.28 },
  lead: { base: 440, harmonics: [1, 0.45, 0.25, 0.12, 0.06], attack: 0.04, release: 0.18 },
  bell: { base: 660, harmonics: [1, 0.7, 0.42, 0.3], ratios: [1, 2.01, 3.97, 6.1], attack: 0.002, release: 1.1 },
  perc: { base: 180, harmonics: [1, 0.65, 0.35], attack: 0.001, release: 0.12, percussive: true },
};

const events = {
  bass: [],
  lead: [],
  bell: [],
  perc: [],
};
for (let i = 0; i < 8; i += 1) {
  events.bass.push({ start: 4.5 + i * 0.75, duration: 0.62, semitones: [0, 0, -5, -7][i % 4], gain: 0.62 });
  events.lead.push({ start: 4.5 + i * 0.375, duration: 0.28, semitones: [0, 4, 7, 4, 9, 7, 4, 0][i], gain: 0.46, brightness: i < 4 ? 1.0 : 0.62 });
  events.bell.push({ start: 4.875 + i * 1.5, duration: 0.9, semitones: [12, 7, 9, 14][i % 4], gain: 0.34 });
  events.perc.push({ start: 4.5 + i * 0.25, duration: 0.16, semitones: (i % 2) * 7, gain: 0.28 });
}

const calibration = [
  { instrument: 'bass', start: 0, end: 1.125, event: { start: 0.2, duration: 0.75, semitones: 0, gain: 0.7 } },
  { instrument: 'lead', start: 1.125, end: 2.25, event: { start: 1.35, duration: 0.75, semitones: 0, gain: 0.55, brightness: 1.0 } },
  { instrument: 'bell', start: 2.25, end: 3.375, event: { start: 2.4, duration: 0.8, semitones: 0, gain: 0.4 } },
  { instrument: 'perc', start: 3.375, end: 4.5, event: { start: 3.5, duration: 0.35, semitones: 0, gain: 0.35 } },
];
for (const item of calibration) events[item.instrument].unshift(item.event);

function envelope(t, duration, attack, release) {
  if (t < 0 || t >= duration) return 0;
  const attackGain = attack <= 0 ? 1 : Math.min(1, t / attack);
  const remaining = duration - t;
  const releaseGain = release <= 0 ? 1 : Math.min(1, remaining / release);
  return Math.min(attackGain, releaseGain);
}

function note(instrument, event, t) {
  const spec = instruments[instrument];
  const frequency = spec.base * 2 ** (event.semitones / 12);
  const e = envelope(t - event.start, event.duration, spec.attack, spec.release) * event.gain;
  if (e === 0) return 0;
  const ratios = spec.ratios ?? spec.harmonics.map((_, index) => index + 1);
  if (spec.percussive) return (Math.sin(pi2 * frequency * (t - event.start)) + 0.35 * Math.sin(pi2 * frequency * 3.7 * (t - event.start))) * e * Math.exp(-18 * (t - event.start));
  let value = 0;
  for (let i = 0; i < spec.harmonics.length; i += 1) value += spec.harmonics[i] * (event.brightness ?? 1) ** i * Math.sin(pi2 * frequency * ratios[i] * (t - event.start));
  return value * e;
}

function writeWav(file, signedPcm) {
  const data = Buffer.alloc(signedPcm.length);
  for (let i = 0; i < signedPcm.length; i += 1) data[i] = Math.max(0, Math.min(255, signedPcm[i] + 128));
  const header = Buffer.alloc(44);
  header.write('RIFF', 0); header.writeUInt32LE(36 + data.length, 4); header.write('WAVE', 8);
  header.write('fmt ', 12); header.writeUInt32LE(16, 16); header.writeUInt16LE(1, 20); header.writeUInt16LE(1, 22);
  header.writeUInt32LE(rate, 24); header.writeUInt32LE(rate, 28); header.writeUInt16LE(1, 32); header.writeUInt16LE(8, 34);
  header.write('data', 36); header.writeUInt32LE(data.length, 40);
  fs.writeFileSync(file, Buffer.concat([header, data]));
}

fs.mkdirSync(out, { recursive: true });
const stems = Object.fromEntries(Object.keys(instruments).map((name) => [name, new Array(samples).fill(0)]));
for (const [instrument, instrumentEvents] of Object.entries(events)) {
  for (let sample = 0; sample < samples; sample += 1) {
    const time = sample / rate;
    for (const event of instrumentEvents) stems[instrument][sample] += note(instrument, event, time);
  }
}
const mix = new Array(samples).fill(0);
for (let sample = 0; sample < samples; sample += 1) for (const stem of Object.values(stems)) mix[sample] += stem[sample];
const peak = mix.reduce((maximum, value) => Math.max(maximum, Math.abs(value)), 1);
const scale = 112 / peak;
const pcm = (values) => values.map((value) => Math.max(-128, Math.min(127, Math.round(value * scale))));
writeWav(path.join(out, 'mix.wav'), pcm(mix));
for (const [name, stem] of Object.entries(stems)) writeWav(path.join(out, `reference-${name}.wav`), pcm(stem));
fs.writeFileSync(path.join(out, 'manifest.json'), JSON.stringify({ rate, samples, mix: 'mix.wav', references: Object.keys(instruments).map((name) => `reference-${name}.wav`), calibration, events }, null, 2));
console.log(JSON.stringify({ out, rate, samples, instruments: Object.keys(instruments), calibration: calibration.length }, null, 2));
