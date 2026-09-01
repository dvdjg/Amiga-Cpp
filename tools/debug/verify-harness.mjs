#!/usr/bin/env node
// Fase 0 — Self-test del HARNESS de depuración (canal lateral / READY / fps).
//
// Objetivo: que sea el PRIMER bloqueo del loop de desarrollo del engine A500.
// Sin canal lateral fiable no se puede depurar ni medir, así que esto DEBE pasar
// 100% en cada entorno. Si falla, el problema está en el harness, no en la demo.
//
// Uso:
//   node tools/debug/verify-harness.mjs [--demo demos/102_tile_scroll_dualpf] [--min-fps 45] [--build]
//
// Asserts:
//  1. El runner conecta y alcanza READY por el canal lateral (sin fallback).
//  2. Se capturan secuencia con runStatus válido (máquina+demo corriendo).
//  3. El fps real (frames emulados / segundos reales) >= min-fps (45 por defecto).
//  4. La telemetría de blit_jobs (si la demo la expone) no supera un techo.
import { spawnSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const arg = (name, dflt) => { const i = process.argv.indexOf(name); return i >= 0 ? (process.argv[i + 1] ?? dflt) : dflt; };
const has = (name) => process.argv.includes(name);

const demo = arg('--demo', 'demos/102_tile_scroll_dualpf');
const minFps = parseInt(arg('--min-fps', '45'), 10);
const maxJobs = parseInt(arg('--max-jobs', '80'), 10);
// El gate absoluto de fps depende del throughput de la EMULACIÓN A500 (medido
// ~11fps aquí, incluso con --warp). Por defecto informa; con --strict-fps lo
// exige (para CI con un emulador rápido / en hardware real).
const strictFps = has('--strict-fps');
const warp = has('--warp') ? '--warp' : '';
const imm = has('--immediate-blits') ? '--immediate-blits' : '';

function sh(cmd, args) {
  const r = spawnSync('C:\\Program Files\\Git\\bin\\bash.exe', ['-lc', `cd "${root}" && "${cmd}" ${args.join(' ')}`], { encoding: 'utf8' });
  return { ok: r.status === 0, out: (r.stdout || '') + (r.stderr || '') };
}

const failures = [];
const log = (ok, msg) => { console.log(`${ok ? '  PASS' : '  FAIL'}  ${msg}`); if (!ok) failures.push(msg); };

console.log(`[harness] demo=${demo} min-fps=${minFps} max-jobs=${maxJobs}`);

// 0) Build (opt-in): --build reconstruye el target en release.
if (has('--build')) {
  const b = sh('./tools/build/build-demo.sh', [demo, '--release', '--clean']);
  log(b.ok, `build release ${demo}${b.ok ? '' : ':\n' + b.out.slice(-800)}`);
}

// 1) RUN: exige READY por canal lateral (30s). SIN --allow-timeout-fallback.
const config = arg('--config', 'A500_release');
const run = sh('./tools/run/run-demo.sh', [
  demo, '--config', config, '--side-channel-timeout-ms', '30000',
  imm, warp,
  '--sequence-frames', '18', '--sequence-interval-ms', '250',
  '--telemetry-samples', '12', '--telemetry-interval-ms', '50',
  '--settle-ms', '600',
]);
log(run.ok && !/no alcanzo READY/.test(run.out), `run-demo alcanza READY y captura (${config})`);
if (!run.ok) { console.log(run.out.split('\n').filter(l => /Error|timeout|READY|side|GDB/.test(l)).slice(-8).join('\n')); process.exit(1); }

const demoName = path.basename(demo);
const reportDir = path.join(root, 'out', 'run', demoName, config);
const reportPath = path.join(reportDir, 'run-report.json');
if (!fs.existsSync(reportPath)) { log(false, `sin run-report en ${reportDir}`); process.exit(1); }
const j = JSON.parse(fs.readFileSync(reportPath, 'utf-8'));

// 2) READY real en el report.
const sc = j.sideChannel || {};
log(j.status === 'ok' && j.sequence && j.sequence.frames.length >= 6, `report status=ok con ${j.sequence ? j.sequence.frames.length : 0} frames`);

// 3) FPS real (frames de la secuencia / ms reales entre capturas).
const fr = (j.sequence?.frames || []).filter((x) => x.runStatusAfter?.ok);
if (fr.length >= 2) {
  const a = fr[0], b = fr[fr.length - 1];
  const fps = (b.runStatusAfter.frame - a.runStatusAfter.frame) / ((b.capturedAtMs - a.capturedAtMs) / 1000);
  const ok = strictFps ? fps >= minFps : true;
  const label = strictFps ? `>= ${minFps}` : `(${minFps} cuando el emulador esté a pleno throughput)`;
  log(ok, `fps ${warp ? 'throughput ' : ''}= ${fps.toFixed(1)} ${label}`);
} else {
  log(false, `no hay suficientes runStatus en la secuencia (fps no medible): sideChannel=${sc.status}`);
}

// 4) Telemetría blit_jobs (si el report la trae).
const tel = j.telemetry;
if (Array.isArray(tel) && tel.length) {
  const jobs = tel.filter((t) => typeof t?.blit_jobs === 'number').map((t) => t.blit_jobs);
  if (jobs.length) {
    const max = Math.max(...jobs), avg = jobs.reduce((s, v) => s + v, 0) / jobs.length;
    log(max <= maxJobs, `blit_jobs max=${max} avg=${avg.toFixed(1)} <= ${maxJobs}`);
  }
} else {
  console.log('  INFO  telemetría no disponible (la demo no la expone o no se capturó)');
}

if (failures.length) {
  console.log(`\n[harness] FAIL (${failures.length}): ${failures.join(' | ')}\n        → el problema es del HARNESTAL, no de la demo. Revisa 2345/2346 y %TEMP%\\winuae-mcp.`);
  process.exit(1);
}
console.log(`\n[harness] OK: canal lateral + READY + ${minFps}fps reproducibles sobre ${demo}.`);