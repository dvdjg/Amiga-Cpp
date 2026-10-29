#!/usr/bin/env node
// Banco de pruebas de configuraciones de una demo (A/B de rendimiento).
//
// Ejecuta `measure-fps.mjs` para cada config, junta fps/ciclos/campos y,
// opcionalmente, el tamaño de los assets, y escribe una tabla Markdown con el
// delta frente a la primera config (la referencia). Es el protocolo de
// coste/beneficio de docs/guides/optimization/OPTIMIZACION_TILED_ANIMADO.md.
//
// Uso:
//   node tools/debug/bench-demo.mjs <demo> --configs A500_release,A500_demo_tour_stop0_release
//        [--duration-ms 12000] [--assets dir1,dir2] [--json salida.json]
//
// Requisitos: cada config debe estar compilada y tener su `runner.uae`
// (`run-demo.sh --config <config>` una vez). El script no compila ni lanza el
// emulador por su cuenta: así el A/B no mezcla builds ni configuraciones.
import fs from 'node:fs';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const argv = process.argv.slice(2);
const positional = [];
const flags = new Map();
for (let i = 0; i < argv.length; ++i) {
	const a = argv[i];
	if (a.startsWith('--')) {
		flags.set(a.slice(2), argv[i + 1] !== undefined && !argv[i + 1].startsWith('--') ? argv[++i] : '1');
	} else {
		positional.push(a);
	}
}
const demo = positional[0];
if (!demo) {
	console.error('uso: bench-demo.mjs <demo> --configs A,B[,C] [--duration-ms N] [--assets dir1,dir2] [--json salida.json]');
	process.exit(2);
}
const configs = (flags.get('configs') || '').split(',').map((s) => s.trim()).filter(Boolean);
if (configs.length === 0) {
	console.error('[bench] falta --configs <A,B,...>');
	process.exit(2);
}
const durationMs = Math.max(1000, parseInt(flags.get('duration-ms') || '12000', 10));
const jsonPath = flags.get('json') || '';

function dirBytes(dir) {
	let total = 0;
	const walk = (p) => {
		for (const entry of fs.readdirSync(p, { withFileTypes: true })) {
			const full = path.join(p, entry.name);
			if (entry.isDirectory()) walk(full);
			else if (entry.isFile()) total += fs.statSync(full).size;
		}
	};
	if (fs.existsSync(dir)) walk(dir);
	return total;
}
const assetBytes = (flags.get('assets') || '').split(',').map((s) => s.trim()).filter(Boolean)
	.reduce((n, dir) => n + dirBytes(path.resolve(ROOT, dir)), 0);

const rows = [];
for (const config of configs) {
	const exe = path.join(ROOT, 'out', 'demos', demo, config, `${demo}.${config}.exe`);
	const runner = path.join(ROOT, 'out', 'run', demo, config, 'runner.uae');
	if (!fs.existsSync(exe)) {
		console.warn(`[bench] ${config}: falta ${exe}; compila esa config`);
		rows.push({ config, error: 'sin ejecutable' });
		continue;
	}
	if (!fs.existsSync(runner)) {
		console.warn(`[bench] ${config}: falta ${runner}; ejecuta run-demo con esa config una vez`);
		rows.push({ config, error: 'sin runner.uae' });
		continue;
	}
	try {
		const out = execFileSync(process.execPath, [
			path.join(ROOT, 'tools/debug/measure-fps.mjs'), demo, config,
			'--json', '--duration-ms', String(durationMs),
		], { encoding: 'utf8', stdio: ['ignore', 'pipe', 'pipe'] });
		const jsonLine = out.trim().split(/\r?\n/).filter((l) => l.startsWith('{')).pop();
		if (jsonLine == null) throw new Error('measure-fps no devolvió JSON');
		const r = JSON.parse(jsonLine);
		rows.push({ config, ...r });
	} catch (error) {
		console.warn(`[bench] ${config}: ${error.message}`);
		rows.push({ config, error: 'medida fallida' });
	}
}

const base = rows.find((r) => !r.error);
const fmt = (v, d = 2) => (Number.isFinite(v) ? v.toFixed(d) : '—');
const pct = (v, ref) => (Number.isFinite(v) && Number.isFinite(ref) && ref !== 0
	? `${((v - ref) / ref * 100).toFixed(1)} %` : '—');

console.log(`# Banco de rendimiento: ${demo}`);
console.log('');
console.log(`- Fecha: ${new Date().toISOString().slice(0, 10)}`);
console.log(`- Duración por config: ${durationMs} ms`);
if (assetBytes > 0) console.log(`- Assets sumados: ${(assetBytes / 1024).toFixed(1)} KB`);
console.log('');
console.log('| Config | fps | ciclos/frame | campos/frame | mín/máx fps | Δfps | Δciclos |');
console.log('|---|---:|---:|---:|---:|---:|---:|');
for (const r of rows) {
	if (r.error) {
		console.log(`| ${r.config} | ${r.error} | — | — | — | — | — |`);
		continue;
	}
	console.log(`| ${r.config} | ${fmt(r.emulatedFps)} | ${fmt(r.cyclesPerFrame, 0)} | ` +
		`${fmt(r.fieldsPerFrame, 3)} | ${fmt(r.minFps)}/${fmt(r.maxFps)} | ` +
		`${base && r !== base ? pct(r.emulatedFps, base.emulatedFps) : 'ref.'} | ` +
		`${base && r !== base ? pct(r.cyclesPerFrame, base.cyclesPerFrame) : 'ref.'} |`);
}
console.log('');
console.log('Criterio de aceptación (ver OPTIMIZACION_TILED_ANIMADO.md): una optimización se acepta si');
console.log('mejora el peor frame (M) ≥10 % o reduce ≥2 KB de Chip sin regresión visual; si no, se documenta y se descarta.');

if (jsonPath) {
	fs.writeFileSync(path.resolve(ROOT, jsonPath), JSON.stringify({ demo, assetBytes, rows }, null, 2));
	console.log(`\n[bench] JSON: ${jsonPath}`);
}
