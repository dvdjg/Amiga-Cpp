#!/usr/bin/env node
// Resume la telemetría por frame publicada por una demo instrumentada
// (`g_eng_frame_telemetry`) y guardada en run-report.json.
//
// Uso:
//   node tools/debug/blitter-report.mjs <demo> [config]
//
// Requiere una build con `EXTRA_DEFINES="-DDEMO_BLIT_STATS=1"` y un run con
// `--telemetry-samples N --telemetry-interval-ms ms`.
import fs from 'node:fs';
import path from 'node:path';

const root = process.cwd();
const demo = process.argv[2];
const configArg = process.argv[3] ?? null;
if (!demo) {
  console.error('Uso: node tools/debug/blitter-report.mjs <demo> [config]');
  process.exit(2);
}
const runRoot = path.join(root, 'out/run', path.basename(demo));
const candidates = fs.existsSync(runRoot)
  ? fs.readdirSync(runRoot, { withFileTypes: true })
    .filter((e) => e.isDirectory())
    .map((e) => path.join(runRoot, e.name, 'run-report.json'))
    .filter((file) => fs.existsSync(file))
  : [];
const selected = configArg != null
  ? path.join(runRoot, configArg, 'run-report.json')
  : candidates.sort((a, b) => fs.statSync(b).mtimeMs - fs.statSync(a).mtimeMs)[0];
if (selected == null || !fs.existsSync(selected)) {
  console.error('[blitter-report] no hay run-report.json; ejecuta run-demo con --telemetry-samples');
  process.exit(3);
}
const report = JSON.parse(fs.readFileSync(selected, 'utf8'));
const samples = Array.isArray(report.telemetry) ? report.telemetry : [];
if (samples.length === 0) {
  console.error('[blitter-report] sin muestras de g_eng_frame_telemetry');
  process.exit(3);
}
console.log(`# Blitter telemetry — ${report.demo}`);
console.log(`Config: ${report.config ?? configArg ?? path.basename(path.dirname(selected))}`);
console.log(`Run: ${path.relative(root, selected)}`);
console.log('');
console.log('| frame | jobs | words | copper words | fillup extra |');
console.log('|---:|---:|---:|---:|---:|');
for (const s of samples) {
  if (s.error != null) {
    console.log(`| - | - | - | - | ${s.error} |`);
    continue;
  }
  console.log(`| ${s.frame} | ${s.blit_jobs} | ${s.blit_words} | ${s.copper_words} | ${s.fillup_extra} |`);
}
const last = samples.filter((s) => !s.error).at(-1);
if (last) {
  console.log('');
  console.log(`Último frame: jobs=${last.blit_jobs}, words=${last.blit_words}, copper_words=${last.copper_words}.`);
}
