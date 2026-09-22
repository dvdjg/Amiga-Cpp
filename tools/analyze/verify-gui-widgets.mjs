#!/usr/bin/env node
/**
 * Verificador visual de la demo 215 (`215_gui_widgets`).
 *
 * Comprueba, sobre la paleta EHB de la demo, que la UI se ha pintado de verdad:
 *  - el relleno del panel (0x248) cubre una zona amplia;
 *  - hay texto (0xfff) y bisel claro (0xeee);
 *  - el anillo de foco (0xf80) esta presente;
 *  - y, en secuencia, que la pista del slider CAMBIA entre frames (animacion) y que
 *    el cambio se concentra en una banda horizontal estrecha (repintado por zona),
 *    no en toda la pantalla.
 *
 * Uso:
 *   node tools/analyze/verify-gui-widgets.mjs --image <screenshot.png>
 *   node tools/analyze/verify-gui-widgets.mjs --sequence-dir <dir>
 * Salida: informe por stdout; exit 0 = OK, 1 = fallo, 2 = uso.
 */
import * as fs from 'fs';
import * as path from 'path';
import { readPng, pixel } from '../../dist/tools/lib/image.js';

const arg = (name, def = '') => {
	const i = process.argv.indexOf(name);
	return i >= 0 && process.argv[i + 1] && !process.argv[i + 1].startsWith('--') ? process.argv[i + 1] : def;
};

const c444 = (v) => [((v >> 8) & 15) * 0x11, ((v >> 4) & 15) * 0x11, (v & 15) * 0x11];
const eq = (r, g, b, v) => { const [R, G, B] = c444(v); return r === R && g === G && b === B; };

const COLORS = { fill: 0x248, text: 0xfff, shine: 0xeee, ring: 0xf80, bg: 0x012, edit: 0x111 };

function count(imagePath) {
	const img = readPng(imagePath);
	const W = img.width, H = img.height;
	const n = { fill: 0, text: 0, shine: 0, ring: 0, bg: 0, edit: 0 };
	for (let y = 0; y < H; y++)
		for (let x = 0; x < W; x++) {
			const [r, g, b] = pixel(img, x, y);
			for (const k in COLORS) if (eq(r, g, b, COLORS[k])) n[k]++;
		}
	return { imagePath, W, H, ...n };
}

/// Diferencia de dos PNG: numero de pixeles distintos y su caja envolvente.
function diff(a, b) {
	const ia = readPng(a), ib = readPng(b);
	const W = ia.width, H = ia.height;
	let n = 0, minx = W, miny = H, maxx = -1, maxy = -1;
	for (let y = 0; y < H; y++)
		for (let x = 0; x < W; x++) {
			const [r0, g0, b0] = pixel(ia, x, y);
			const [r1, g1, b1] = pixel(ib, x, y);
			if (r0 !== r1 || g0 !== g1 || b0 !== b1) {
				n++;
				if (x < minx) minx = x; if (x > maxx) maxx = x;
				if (y < miny) miny = y; if (y > maxy) maxy = y;
			}
		}
	return { n, w: maxx - minx + 1, h: maxy - miny + 1 };
}

let failures = 0;
const check = (ok, msg) => { console.log(`  ${ok ? 'OK  ' : 'FAIL'} ${msg}`); if (!ok) failures++; };

const image = arg('--image');
const seqDir = arg('--sequence-dir');

if (image) {
	const s = count(image);
	console.log(`verify-gui-widgets: ${path.basename(image)} (${s.W}x${s.H})`);
	check(s.fill > 10000, `relleno de panel presente (${s.fill} px)`);
	check(s.text > 500, `texto presente (${s.text} px)`);
	check(s.shine > 200, `bisel claro presente (${s.shine} px)`);
	check(s.ring > 10, `anillo de foco presente (${s.ring} px)`);
	check(s.bg > 1000, `fondo presente (${s.bg} px)`);
} else if (seqDir) {
	if (!fs.existsSync(seqDir)) { console.error(`No existe ${seqDir}`); process.exit(2); }
	const frames = fs.readdirSync(seqDir).filter((f) => /^frame_\d+\.png$/.test(f)).sort();
	console.log(`verify-gui-widgets: secuencia ${path.basename(seqDir)} (${frames.length} frames)`);
	check(frames.length >= 2, `frames suficientes (${frames.length})`);
	const results = frames.map((f) => count(path.join(seqDir, f)));
	for (const r of results) {
		console.log(`    ${path.basename(r.imagePath)}: panel=${r.fill} texto=${r.text} bisel=${r.shine} foco=${r.ring}`);
	}
	check(results.every((r) => r.fill > 10000 && r.text > 500), 'panel y texto en todos los frames');
	const d = diff(path.join(seqDir, frames[0]), path.join(seqDir, frames[frames.length - 1]));
	console.log(`    diff primer/ultimo frame: ${d.n} px, caja ${d.w}x${d.h}`);
	check(d.n > 100, `la UI anima entre frames (${d.n} px cambian)`);
	check(d.h > 0 && d.h < 60, `el cambio es una banda horizontal (repintado por zona, alto ${d.h})`);
} else {
	console.error('Uso: verify-gui-widgets.mjs --image <png> | --sequence-dir <dir>');
	process.exit(2);
}

if (failures === 0) {
	console.log('OK: demo 215 verificada (widgets + slider animado).');
	process.exit(0);
}
console.log(`FAIL: ${failures} comprobacion(es) fallaron`);
process.exit(1);
