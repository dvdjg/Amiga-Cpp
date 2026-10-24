#!/usr/bin/env node
// Gate de regresion para demos 3D: exige secuencia con movimiento y sin saltos
// de area. Si no hay secuencias capturadas (host CI), avisa y no falla salvo
// --strict, que se usa en el pipeline on-target.
//
//   node tools/check/3d-demo-gate.mjs [--strict] [dir ...]
//
// Sin argumentos revisa las demos 3D conocidas en out/run/<demo>/{A500_release,
// A500_debug}/sequence.
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(here, '..', '..');
const verify = path.join(root, 'tools', 'analyze', 'verify-3d-sequence.mjs');
const strict = process.argv.includes('--strict');
const explicit = process.argv.slice(2).filter((a) => !a.startsWith('--'));
const demos = ['090_polyhedra_orbit', '091_scene3d_nodes', '092_articulated_character', '095_collision3d', '096_silhouette_swarm'];
const configs = ['A500_release', 'A500_debug'];
let checked = 0;
let failed = 0;
for (const demo of explicit.length ? explicit : demos) {
	const candidates = [];
	if (fs.existsSync(demo)) candidates.push(demo);
	else for (const cfg of configs) candidates.push(path.join(root, 'out', 'run', demo, cfg, 'sequence'));
	const dir = candidates.find((d) => fs.existsSync(d));
	if (!dir) {
		console.log(`[3d-gate] SKIP ${demo}: sin secuencia`);
		if (strict) ++failed;
		continue;
	}
	++checked;
	const r = spawnSync(process.execPath, [verify, dir, '--min-frames', '3', '--require-motion'], {
		encoding: 'utf8',
	});
	const ok = r.status === 0;
	console.log(`[3d-gate] ${ok ? 'OK' : 'FAIL'} ${demo}: ${(r.stdout || r.stderr || '').trim()}`);
	if (!ok) ++failed;
}
if (failed) {
	console.error(`[3d-gate] ${failed} demo(s) fallan`);
	process.exit(1);
}
console.log(`[3d-gate] OK: ${checked} secuencia(s) con movimiento`);