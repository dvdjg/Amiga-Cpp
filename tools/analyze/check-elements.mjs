#!/usr/bin/env node
// Conteo objetivo de elementos por color y diff por bandas sobre una secuencia de frames
// (evidencia para el informe de validación de una demo; ver
// docs/guides/methodology/DEMO_VISUAL_DEBUG.md §6.4 y PROCEDIMIENTO_DEMOS_Y_JUEGOS.md).
//
// Uso:
//   node tools/analyze/check-elements.mjs <dir_seq> <#RRGGBB,...> [frameA.png frameB.png]
//   node tools/analyze/check-elements.mjs <dir_seq> <#RRGGBB,...> --expect <color>=<min> [--expect ...]
//
//   - Imprime, por frame, cuántos píxeles hay de cada color EXACTO de la lista (elementos
//     esperados: p. ej. "#00ff00,#ffffff,#ff4400"). Sirve para ver si un elemento
//     aparece/desaparece o queda a medias a lo largo de la secuencia.
//   - Con `--expect <color>=<min>` FALLA (exit 1) si en algún frame el color baja de `min`
//     píxeles: gate de PRESENCIA de elementos (DT-002 de ROADMAP_DEUDA_TECNICA).
//   - Si se dan dos frames, imprime además el diff medio por bandas de 16 px (PNG 2x),
//     marcando las bandas ESTÁTICAS: zonas que no cambian mientras el resto sí.
import fs from 'node:fs';
import path from 'node:path';
import { createRequire } from 'node:module';

const require = createRequire(import.meta.url);
const { PNG } = require('pngjs');

const dir = process.argv[2];
const colorsArg = process.argv[3];
const rest = process.argv.slice(4);
const expects = [];
for (let i = 0; i < rest.length; i++) {
	if (rest[i] === '--expect' && rest[i + 1]) {
		const m = rest[i + 1].match(/^#?([0-9a-fA-F]{6})=(\d+)$/);
		if (!m) {
			console.error(`--expect inválido: ${rest[i + 1]} (usa hex=min)`);
			process.exit(2);
		}
		expects.push({ hex: '#' + m[1].toLowerCase(), min: parseInt(m[2], 10) });
		i++;
	}
}
const positional = rest.filter((v) => v !== '--expect' && !/^#?[0-9a-fA-F]{6}=\d+$/.test(v));
const frameA = positional[0];
const frameB = positional[1];
if (!dir || !colorsArg) {
	console.error('uso: node tools/analyze/check-elements.mjs <dir_seq> <#RRGGBB,...> [frameA.png frameB.png]');
	process.exit(2);
}

function parseColor(s) {
	const m = s.trim().match(/^#?([0-9a-fA-F]{6})$/);
	if (!m) {
		console.error(`color inválido: ${s} (usa #RRGGBB)`);
		process.exit(2);
	}
	const v = parseInt(m[1], 16);
	return { r: (v >> 16) & 0xff, g: (v >> 8) & 0xff, b: v & 0xff };
}

const colors = colorsArg.split(',').filter(Boolean).map((s) => ({ hex: s.trim(), ...parseColor(s) }));
const files = fs
	.readdirSync(dir)
	.filter((f) => /^frame_\d{3}(_f\d+)?\.png$/.test(f))
	.sort();
if (files.length === 0) {
	console.error(`sin frames en ${dir}`);
	process.exit(1);
}

console.log(`# check-elements ${dir}`);
console.log(`# colores: ${colors.map((c) => c.hex).join(' ')}`);
const minCounts = colors.map(() => Infinity);
for (const f of files) {
	const png = PNG.sync.read(fs.readFileSync(path.join(dir, f)));
	const counts = colors.map(() => 0);
	for (let i = 0; i < png.data.length; i += 4) {
		for (let c = 0; c < colors.length; c++) {
			if (png.data[i] === colors[c].r && png.data[i + 1] === colors[c].g &&
			    png.data[i + 2] === colors[c].b) {
				counts[c]++;
				break;
			}
		}
	}
	for (let c = 0; c < counts.length; c++) if (counts[c] < minCounts[c]) minCounts[c] = counts[c];
	console.log(`${f} ${counts.map((n, c) => `${colors[c].hex}=${n}`).join(' ')}`);
}

if (expects.length > 0) {
	const fails = [];
	for (const e of expects) {
		const norm = (s) => s.replace(/^#/, '').toLowerCase();
		const ci = colors.findIndex((c) => norm(c.hex) === norm(e.hex));
		if (ci < 0) {
			fails.push(`--expect ${e.hex}: el color no está en la lista`);
			continue;
		}
		if (minCounts[ci] < e.min) {
			fails.push(`--expect ${e.hex} >= ${e.min}: mínimo visto ${minCounts[ci]}`);
		}
	}
	if (fails.length > 0) {
		for (const f of fails) console.error(`[check-elements] FAIL ${f}`);
		process.exit(1);
	}
	console.log(`# expect OK: ${expects.map((e) => `${e.hex}>=${e.min}`).join(' ')}`);
}

if (frameA && frameB) {
	const a = PNG.sync.read(fs.readFileSync(path.join(dir, frameA)));
	const b = PNG.sync.read(fs.readFileSync(path.join(dir, frameB)));
	console.log(`# band-diff ${frameA} vs ${frameB} (bandas de 16 px del PNG 2x)`);
	for (let y = 0; y < a.height; y += 16) {
		let sum = 0;
		let n = 0;
		for (let yy = y; yy < Math.min(y + 16, a.height); yy++) {
			for (let x = 0; x < a.width; x++) {
				const i = (yy * a.width + x) * 4;
				sum += Math.abs(a.data[i] - b.data[i]) + Math.abs(a.data[i + 1] - b.data[i + 1]) +
				       Math.abs(a.data[i + 2] - b.data[i + 2]);
				n += 3;
			}
		}
		const mean = sum / n;
		console.log(`banda y=${String(y).padStart(3)}..${String(y + 15).padStart(3)}  diff_medio=${mean.toFixed(2)}${mean < 1 ? '  <== ESTATICA' : ''}`);
	}
}
