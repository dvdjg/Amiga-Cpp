#!/usr/bin/env node
/**
 * Verificador visual de la demo 300 (`300_gui_compositor`).
 *
 * Comprueba, sobre la paleta EHB de la demo, que el compositor pinta ventanas (relleno +
 * barra de titulo + texto) y que **se mueven** entre frames consecutivos (la composicion
 * se actualiza: los backings se recomponen en la pantalla).
 *
 * Uso:
 *   node tools/analyze/verify-gui-compositor.mjs --image <screenshot.png>
 *   node tools/analyze/verify-gui-compositor.mjs --sequence-dir <dir>
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

const COLORS = { desktop: 0x012, winbg: 0x248, title: 0x46a, text: 0xfff };

function count(imagePath) {
	const img = readPng(imagePath);
	const W = img.width, H = img.height;
	const n = { desktop: 0, winbg: 0, title: 0, text: 0 };
	for (let y = 0; y < H; y++)
		for (let x = 0; x < W; x++) {
			const [r, g, b] = pixel(img, x, y);
			for (const k in COLORS) if (eq(r, g, b, COLORS[k])) n[k]++;
		}
	return { imagePath, W, H, ...n };
}

function diff(a, b) {
	const ia = readPng(a), ib = readPng(b);
	const W = ia.width, H = ia.height;
	let n = 0;
	for (let y = 0; y < H; y++)
		for (let x = 0; x < W; x++) {
			const [r0, g0, b0] = pixel(ia, x, y);
			const [r1, g1, b1] = pixel(ib, x, y);
			if (r0 !== r1 || g0 !== g1 || b0 !== b1) n++;
		}
	return n;
}

let failures = 0;
const check = (ok, msg) => { console.log(`  ${ok ? 'OK  ' : 'FAIL'} ${msg}`); if (!ok) failures++; };

const image = arg('--image');
const seqDir = arg('--sequence-dir');

if (image) {
	const s = count(image);
	console.log(`verify-gui-compositor: ${path.basename(image)} (${s.W}x${s.H})`);
	check(s.winbg > 8000, `relleno de ventana presente (${s.winbg} px)`);
	check(s.title > 1000, `barra de titulo presente (${s.title} px)`);
	check(s.text > 300, `texto presente (${s.text} px)`);
	check(s.desktop > 1000, `escritorio presente (${s.desktop} px)`);
} else if (seqDir) {
	if (!fs.existsSync(seqDir)) { console.error(`No existe ${seqDir}`); process.exit(2); }
	const frames = fs.readdirSync(seqDir).filter((f) => /^frame_\d+\.png$/.test(f)).sort();
	console.log(`verify-gui-compositor: secuencia ${path.basename(seqDir)} (${frames.length} frames)`);
	check(frames.length >= 3, `frames suficientes (${frames.length})`);
	const results = frames.map((f) => count(path.join(seqDir, f)));
	for (const r of results) {
		console.log(`    ${path.basename(r.imagePath)}: ventana=${r.winbg} titulo=${r.title} texto=${r.text}`);
	}
	// La captura puede caer a mitad del compose (el fondo se limpia antes de copiar los backings),
	// asi que se exige que las ventanas esten en la mayoria de frames, no en todos.
	const withWin = results.filter((r) => r.winbg > 6000 && r.title > 1000).length;
	check(withWin >= Math.ceil(results.length / 2),
	      `ventanas y titulos en la mayoria de frames (${withWin}/${results.length})`);
	let moved = 0;
	for (let i = 1; i < frames.length; ++i) {
		const d = diff(path.join(seqDir, frames[i - 1]), path.join(seqDir, frames[i]));
		console.log(`    diff ${frames[i - 1]} -> ${frames[i]}: ${d} px`);
		if (d > 500) moved++;
	}
	check(moved >= 1, `las ventanas se mueven entre frames (${moved}/${frames.length - 1} pares)`);
} else {
	console.error('Uso: verify-gui-compositor.mjs --image <png> | --sequence-dir <dir>');
	process.exit(2);
}

if (failures === 0) {
	console.log('OK: demo 300 verificada (compositor: ventanas movibles).');
	process.exit(0);
}
console.log(`FAIL: ${failures} comprobacion(es) fallaron`);
process.exit(1);
