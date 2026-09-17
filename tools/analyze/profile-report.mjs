#!/usr/bin/env node
// Informe de un `.amigaprofile` de vscode-amiga-debug: es un **CPU profile de Chrome
// DevTools** con un unico frame agregado que trae `nodes` (con `callFrame.functionName` y
// su jerarquia) + `samples` (un indice de nodo por muestra) + `timeDeltas` (microsegundos
// entre muestras) + `$amiga` (registros custom del frame). Agrega el **tiempo real** por
// rutina y emite el top; con `--json` produce una tabla compacta apta para pasar a un
// modelo local (Ollama) sin gastar contexto.
//
// Uso:
//   node tools/analyze/profile-report.mjs <perfil.amigaprofile> [--top 20] [--json]
//
// Nota: el parser del MCP (`parseProfile`) trabaja sobre el binario, no sobre este JSON;
// este informe lee el JSON del plugin directamente. Para nombre+archivo+linea exactos se
// usa el subarbol de cada nodo; `hitCount` decide cuando no hay `timeDeltas`.

import * as fs from 'fs';

const args = process.argv.slice(2);
const file = args.find((a) => !a.startsWith('--'));
if (!file) {
  console.error('Uso: node tools/analyze/profile-report.mjs <perfil.amigaprofile> [--top N] [--json]');
  process.exit(1);
}
const topN = (() => {
  const i = args.indexOf('--top');
  return i >= 0 ? parseInt(args[i + 1] || '20', 10) : 20;
})();
const asJson = args.includes('--json');

const doc = JSON.parse(fs.readFileSync(file, 'utf8'));
// El plugin agrega todo en `firstFrame`; se aceptan tambien `frames[]`/raiz plana.
const profiles = [];
if (doc.firstFrame && doc.firstFrame.nodes) profiles.push(doc.firstFrame);
if (Array.isArray(doc.frames)) profiles.push(...doc.frames.filter((f) => f && f.nodes));
if (doc.nodes) profiles.push(doc);

const selfByRoutine = new Map();
const selfByFile = new Map();
const inclByRoutine = new Map();
let totalUs = 0;
let totalSamples = 0;
let totalIdle = 0;

for (const prof of profiles) {
  const byId = new Map(prof.nodes.map((n) => [n.id, n]));
  const usPerNode = new Map(); // tiempo propio por nodo

  const samples = prof.samples || [];
  const deltas = prof.timeDeltas || [];
  if (samples.length) {
    for (let i = 0; i < samples.length; i++) {
      const id = samples[i];
      const dt = deltas[i] ?? 0;
      usPerNode.set(id, (usPerNode.get(id) || 0) + dt);
      totalUs += dt;
    }
    totalSamples += samples.length;
  } else {
    // Sin muestras: se cae a hitCount como medida relativa.
    for (const n of prof.nodes) {
      if (n.hitCount) {
        usPerNode.set(n.id, (usPerNode.get(n.id) || 0) + n.hitCount);
        totalUs += n.hitCount;
      }
    }
    totalSamples += prof.nodes.reduce((a, n) => a + (n.hitCount || 0), 0);
  }

  // Inclusive (subarbol) para ordenar por coste acumulado.
  const inc = new Map();
  const order = [];
  const roots = prof.nodes.filter((n) => !prof.nodes.some((m) => (m.children || []).includes(n.id)));
  const stack = roots.length ? [...roots] : (prof.nodes.length ? [prof.nodes[0]] : []);
  while (stack.length) {
    const n = stack.pop();
    order.push(n);
    for (const c of n.children || []) {
      const cn = byId.get(c);
      if (cn) stack.push(cn);
    }
  }
  for (let i = order.length - 1; i >= 0; --i) {
    const n = order[i];
    let s = usPerNode.get(n.id) || 0;
    for (const c of n.children || []) s += inc.get(c) || 0;
    inc.set(n.id, s);
  }

  const keyOf = (n) => {
    const cf = n.callFrame || {};
    const fn = cf.functionName || '(anonimo)';
    const url = (cf.url || '').replace(/\\/g, '/');
    const short = url ? url.split('/').slice(-2).join('/') : '';
    const loc = cf.lineNumber >= 0 ? `:${cf.lineNumber + 1}` : '';
    const inline = /\(inlined\)/.test(fn) ? ' (inlined)' : '';
    return { fn, url: short || '(sin archivo)', label: short ? `${fn} @ ${short}${loc}` : fn + inline };
  };
  for (const n of prof.nodes) {
    const self = usPerNode.get(n.id) || 0;
    if (self === 0) continue;
    const { fn, url, label } = keyOf(n);
    selfByRoutine.set(label, (selfByRoutine.get(label) || 0) + self);
    selfByFile.set(url, (selfByFile.get(url) || 0) + self);
    inclByRoutine.set(label, (inclByRoutine.get(label) || 0) + (inc.get(n.id) || 0));
    if (fn === '[IRQ]') totalIdle += self;
  }
}

const pct = (v) => (totalUs ? (100 * v / totalUs).toFixed(1) : '0.0');
const top = (m, n) => [...m.entries()].sort((a, b) => b[1] - a[1]).slice(0, n);
const fmtUs = (us) => {
  const cycles = us * 7.09379; // periferico de depuracion A500: ~7,09379 MHz
  return `${(us / 1000).toFixed(1)}ms / ${Math.round(cycles)} ciclos`;
};

if (asJson) {
  console.log(JSON.stringify({
    file: file.split(/[\\/]/).pop(),
    profiles: profiles.length,
    samples: totalSamples,
    totalUs,
    totalCycles: Math.round(totalUs * 7.09379),
    irqUs: totalIdle,
    byRoutine: top(selfByRoutine, topN).map(([k, v]) => ({ routine: k, us: Math.round(v), pct: Number(pct(v)) })),
    byFile: top(selfByFile, topN).map(([k, v]) => ({ file: k, us: Math.round(v), pct: Number(pct(v)) })),
  }, null, 1));
} else {
  console.log(`[prof] ${file.split(/[\\/]/).pop()} | ${profiles.length} perfil(es) | ${totalSamples} muestras | ${fmtUs(totalUs)} (1 frame PAL = 141876)`);
  console.log('  --- top por rutina (tiempo propio) ---');
  for (const [k, v] of top(selfByRoutine, topN)) {
    console.log(`  ${pct(v).padStart(5)}%  ${fmtUs(v).padEnd(26)}  ${k}`);
  }
  console.log('  --- top por archivo ---');
  for (const [k, v] of top(selfByFile, 12)) {
    console.log(`  ${pct(v).padStart(5)}%  ${fmtUs(v).padEnd(26)}  ${k}`);
  }
}
