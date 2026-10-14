#!/usr/bin/env node
// Inventario de fps de la flota (DT-009): ejecuta `measure-fps.mjs` sobre una lista de demos
// (o todas las construidas) y vuelca una tabla (demo, fps, fields, ciclos, jobs) en
// `out/fleet-fps/` (JSON + Markdown). Secuencial: measure-fps usa los puertos del runner.
//
// Uso: node tools/debug/audit-demos-fps.mjs [demoId ...] [--all]
//   Sin argumentos: lista clave (las demos con deuda/estado interesante).
//   --all: todas las que tengan build A500_debug bajo out/demos/.
import * as fs from 'node:fs';
import * as path from 'node:path';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(__dirname, '../..');
const OUT = path.join(ROOT, 'out/fleet-fps');
fs.mkdirSync(OUT, { recursive: true });

const KEY_LIST = [
	'engine_amiga_065_mechanisms_bridge', 'engine_amiga_066_bob_showcase', '085_copper_plan_scene',
	'101_ehb_tile_scroll_driver', '110_ylimited_shooter', '116_flatshade_convex',
	'126_fast_bobs', '202_xlimited_dpf', '212_free_scroll_layer',
	'213_spr_layer', '215_jim_power', '218_free_form_sprite_layer',
];

const args = process.argv.slice(2);
let demos = args.filter((a) => !a.startsWith('--'));
if (args.includes('--all')) {
	const base = path.join(ROOT, 'out/demos');
	demos = fs.readdirSync(base).filter((d) =>
		fs.existsSync(path.join(base, d, 'A500_debug')),
	);
}
if (demos.length === 0) demos = KEY_LIST;

const rows = [];
for (const demo of demos) {
	process.stdout.write(`[fleet-fps] ${demo} ... `);
	let json = null;
	for (let attempt = 0; attempt < 2 && json === null; ++attempt) {
		try {
			const out = execFileSync(
				'node',
				[path.join(ROOT, 'tools/debug/measure-fps.mjs'), demo, 'A500_debug', '--json'],
				{ cwd: ROOT, encoding: 'utf8', stdio: ['ignore', 'pipe', 'ignore'] },
			);
			json = JSON.parse(out.trim().split('\n').pop());
		} catch {
			await new Promise((r) => setTimeout(r, 3000)); // reintento: puertos del runner
		}
	}
	if (json !== null) {
		rows.push({
			demo,
			fps: json.emulatedFps,
			fields: Number(json.fieldsPerFrame?.toFixed?.(2) ?? json.fieldsPerFrame),
			cycles: json.cyclesPerFrame,
			frames: json.frames,
			detail: json.detail,
		});
		console.log(`${json.emulatedFps} fps (${json.fieldsPerFrame} campos)`);
	} else {
		rows.push({ demo, fps: 0, fields: null, cycles: 0, frames: 0, detail: 'FALLO' });
		console.log('FALLO (no READY / medida)');
	}
}

fs.writeFileSync(path.join(OUT, 'fleet-fps.json'), JSON.stringify(rows, null, 2), 'utf8');
const md = [
	'| demo | fps | campos/frame | ciclos/frame | jobs |',
	'|---|---|---|---|---|',
	...rows.map(
		(r) =>
			`| ${r.demo} | ${r.fps} | ${r.fields ?? '-'} | ${r.cycles} | ${r.detail ?? '-'} |`,
	),
];
fs.writeFileSync(path.join(OUT, 'fleet-fps.md'), md.join('\n') + '\n', 'utf8');
console.log(`[fleet-fps] tabla en out/fleet-fps/fleet-fps.md (${rows.length} demos)`);
