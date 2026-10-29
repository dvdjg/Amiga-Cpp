#!/usr/bin/env node
// Detector de artefactos oscuros en una captura: busca clusters de píxeles
// casi negros (triángulos/cuñas, bloques sólidos, huecos) y, si se pasa una
// referencia, solo los que **no estaban** en ella. Complementa la pasada de
// visión: el modelo local es poco fiable con detalles de 16 px, así que un
// negativo suyo no invalida un cluster medido aquí (y viceversa: un cluster es
// un candidato a mirar, no una condena).
//
// Uso:
//   node tools/analyze/dark-clusters.mjs <frame.png> [--reference base.png]
//        [--max-luma 48] [--min-pixels 12] [--region x,y,w,h]
//        [--overlay salida.png] [--json]
//
// `--reference` debe tener el mismo tamaño. Un píxel cuenta como oscuro si su
// luminancia < `--max-luma`; con referencia, solo si el píxel de la referencia
// es claramente más claro (no se marcan los contornos oscuros que ya existían).
// La región por defecto es la imagen entera; en capturas con HUD de host y
// bandas negras, pasar el rectángulo del playfield (p. ej. `56,30,640,416`).
import fs from 'node:fs';
import { createRequire } from 'node:module';
const require = createRequire(import.meta.url);
const { PNG } = require('pngjs');

const argv = process.argv.slice(2);
const positional = [];
const opt = new Map();
for (let i = 0; i < argv.length; ++i) {
	const a = argv[i];
	if (a === '--json') opt.set('json', '1');
	else if (a.startsWith('--')) opt.set(a.slice(2), argv[++i]);
	else positional.push(a);
}
const framePath = positional[0];
if (!framePath) {
	console.error('uso: dark-clusters.mjs <frame.png> [--reference base.png] [--max-luma 48] [--min-pixels 12] [--region x,y,w,h] [--overlay out.png]');
	process.exit(2);
}
const maxLuma = Math.max(0, parseInt(opt.get('max-luma') || '48', 10));
const minPixels = Math.max(1, parseInt(opt.get('min-pixels') || '12', 10));
const frame = PNG.sync.read(fs.readFileSync(framePath));
const referencePath = opt.get('reference') || '';
const reference = referencePath ? PNG.sync.read(fs.readFileSync(referencePath)) : null;
if (reference && (reference.width !== frame.width || reference.height !== frame.height)) {
	console.error('[dark-clusters] la referencia debe tener el mismo tamaño que el frame');
	process.exit(2);
}
const [rx, ry, rw, rh] = (opt.get('region') || `0,0,${frame.width},${frame.height}`)
	.split(',').map((v) => parseInt(v, 10));
const x0 = Math.max(0, rx);
const y0 = Math.max(0, ry);
const x1 = Math.min(frame.width, rx + rw);
const y1 = Math.min(frame.height, ry + rh);
const W = frame.width;
const luma = (img, i) => 0.299 * img.data[i] + 0.587 * img.data[i + 1] + 0.114 * img.data[i + 2];
const dark = new Uint8Array(W * frame.height);
for (let y = y0; y < y1; ++y) {
	for (let x = x0; x < x1; ++x) {
		const i = (y * W + x) * 4;
		if (luma(frame, i) >= maxLuma) continue;
		if (reference && luma(reference, i) < maxLuma) continue; // ya era oscuro
		dark[y * W + x] = 1;
	}
}
const seen = new Uint8Array(W * frame.height);
const clusters = [];
for (let y = y0; y < y1; ++y) {
	for (let x = x0; x < x1; ++x) {
		const start = y * W + x;
		if (!dark[start] || seen[start]) continue;
		const stack = [start];
		seen[start] = 1;
		let n = 0;
		let minx = x, maxx = x, miny = y, maxy = y;
		while (stack.length) {
			const j = stack.pop();
			++n;
			const jx = j % W;
			const jy = (j / W) | 0;
			if (jx < minx) minx = jx;
			if (jx > maxx) maxx = jx;
			if (jy < miny) miny = jy;
			if (jy > maxy) maxy = jy;
			for (const [dx, dy] of [[1, 0], [-1, 0], [0, 1], [0, -1]]) {
				const nx = jx + dx, ny = jy + dy;
				if (nx < x0 || ny < y0 || nx >= x1 || ny >= y1) continue;
				const k = ny * W + nx;
				if (dark[k] && !seen[k]) { seen[k] = 1; stack.push(k); }
			}
		}
		if (n >= minPixels) clusters.push({ pixels: n, left: minx, top: miny, right: maxx + 1, bottom: maxy + 1 });
	}
}
clusters.sort((a, b) => b.pixels - a.pixels);
const overlayPath = opt.get('overlay') || '';
if (overlayPath) {
	const out = new PNG({ width: frame.width, height: frame.height });
	frame.data.copy(out.data);
	for (let y = y0; y < y1; ++y) {
		for (let x = x0; x < x1; ++x) {
			if (!dark[y * W + x]) continue;
			const i = (y * W + x) * 4;
			out.data[i] = 255; out.data[i + 1] = 0; out.data[i + 2] = 255; out.data[i + 3] = 255;
		}
	}
	fs.writeFileSync(overlayPath, PNG.sync.write(out));
}
if (opt.has('json')) {
	console.log(JSON.stringify({ frame: framePath, reference: referencePath || null, maxLuma, minPixels, region: [x0, y0, x1, y1], clusters }, null, 2));
} else {
	console.log(`[dark-clusters] ${framePath}`);
	if (referencePath) console.log(`  referencia: ${referencePath}`);
	console.log(`  región ${x0},${y0}..${x1},${y1}; luma < ${maxLuma}; clusters >= ${minPixels} px`);
	if (clusters.length === 0) {
		console.log('  OK: sin clusters oscuros nuevos.');
	} else {
		for (const c of clusters) {
			console.log(`  CLUSTER ${c.pixels} px  bbox ${c.left},${c.top}..${c.right},${c.bottom}`);
		}
		console.log('  Los clusters son CANDIDATOS a mirar con un recorte + Ollama; no son un veredicto por sí solos.');
	}
}

