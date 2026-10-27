#!/usr/bin/env node
// Resume el bloque general `g_eng_metrics` guardado por run-demo en
// run-report.json (build con -DENG_METRICS=1). No requiere WinUAE abierto.
//
// Uso:
//   node tools/debug/metrics-report.mjs <demoId> [config] [--json]
//
// El orden de `values` es el de `eng::debug::Metric` en
// engine/include/eng/debug/metrics.hpp.

import { existsSync, readFileSync } from 'node:fs';
import { join } from 'node:path';

const args = process.argv.slice(2);
if (args.length === 0 || args.includes('--help')) {
  console.log('uso: node tools/debug/metrics-report.mjs <demoId> [config] [--json]');
  process.exit(1);
}
const demo = args[0];
const config = args.find((a) => !a.startsWith('--') && a !== demo) ?? 'A500_release';
const asJson = args.includes('--json');
const reportPath = join('out', 'run', demo, config, 'run-report.json');
if (!existsSync(reportPath)) {
  console.error(`[metrics-report] no existe ${reportPath} (ejecuta run-demo primero)`);
  process.exit(2);
}

const report = JSON.parse(readFileSync(reportPath, 'utf8'));
const samples = Array.isArray(report.metrics) ? report.metrics.filter((s) => !s.error) : [];
if (samples.length === 0) {
  console.error('[metrics-report] sin muestras de g_eng_metrics; compila con -DENG_METRICS=1');
  process.exit(3);
}

const names = [
  'blit_jobs', 'blit_words', 'blit_masked_jobs', 'blit_copy_jobs',
  'copper_words', 'fillup_extra',
  'tiled_groups_scanned', 'tiled_groups_patched', 'tiled_groups_deferred',
  'tiled_cells_patched', 'tiled_cells_culled', 'tiled_base_restores',
  'object_draws', 'actor_draws', 'actor_restores', 'guard_overlays',
];
const eventName = (id) => names[id] ?? `metric_${id}`;
const last = samples.at(-1);

if (asJson) {
  console.log(JSON.stringify({ demo, config, samples, last }, null, 2));
  process.exit(0);
}

console.log(`# ${demo} (${config})`);
console.log('');
console.log('| frame | contador | valor |');
console.log('|---:|---|---:|');
for (let i = 0; i < last.values.length; ++i) {
  console.log(`| ${last.frame} | ${eventName(i)} | ${last.values[i]} |`);
}
console.log('');
console.log(`eventos=${last.event_total} (anillo ${last.event_count}/16)`);
for (const e of last.events) {
  console.log(`  f${e.frame}  ${eventName(e.metric)}=${e.value}`);
}
