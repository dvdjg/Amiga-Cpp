#!/usr/bin/env node
/**
 * Verificador visual de la demo 077 (`077_math3d_cube`).
 *
 * Comprueba, sobre la paleta EHB de la demo, que:
 *  - la captura tiene "pantalla" (hay zona no negra) y fondo (0x012);
 *  - el marco fijo (0x0ff) está presente;
 *  - hay píxeles del cubo (rampa 0x111..0x9bf) y, en secuencia, que el cubo
 *    CAMBIA entre frames (animación) y usa más de un nivel de sombreado.
 *
 * Uso:
 *   node tools/analyze/verify-math3d-cube.mjs --image <screenshot.png>
 *   node tools/analyze/verify-math3d-cube.mjs --sequence-dir <dir>
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
// Rampas de las demos 077 (alambre) y 078 (solido) mapeadas a niveles 1..7.
const SHADES = {
	0x111: 1, 0x22a: 2, 0x33c: 3, 0x44e: 4, 0x55f: 5, 0x77f: 6, 0x9bf: 7,
	0x123: 1, 0x246: 2, 0x358: 3, 0x47a: 4, 0x58c: 5, 0x6ae: 6, 0x8cf: 7,
};
const isCube = (r, g, b) => Object.keys(SHADES).some((k) => eq(r, g, b, Number(k)));

function analyze(imagePath) {
	const img = readPng(imagePath);
	const W = img.width, H = img.height;
	let minx = W, miny = H, maxx = 0, maxy = 0;
	for (let y = 0; y < H; y++)
		for (let x = 0; x < W; x++) {
			const [r, g, b] = pixel(img, x, y);
			if (!(r < 8 && g < 8 && b < 8)) {
				if (x < minx) minx = x; if (x > maxx) maxx = x;
				if (y < miny) miny = y; if (y > maxy) maxy = y;
			}
		}
	let cubePx = 0, framePx = 0, bgPx = 0;
	const shades = new Set();
	let cx0 = W, cy0 = H, cx1 = 0, cy1 = 0;
	for (let y = miny; y <= maxy; y++)
		for (let x = minx; x <= maxx; x++) {
			const [r, g, b] = pixel(img, x, y);
			if (isCube(r, g, b)) {
				cubePx++;
				for (const k in SHADES) if (eq(r, g, b, Number(k))) shades.add(SHADES[k]);
				if (x < cx0) cx0 = x; if (x > cx1) cx1 = x;
				if (y < cy0) cy0 = y; if (y > cy1) cy1 = y;
			} else if (eq(r, g, b, 0x0ff)) framePx++;
			else if (eq(r, g, b, 0x012)) bgPx++;
		}
	const cbox = cubePx > 0 ? { w: cx1 - cx0 + 1, h: cy1 - cy0 + 1 } : { w: 0, h: 0 };
	// Firma gruesa del cubo: caja redondeada a 4 px + nº de píxeles por franja.
	const sig = cubePx > 0 ? `${Math.round(cbox.w / 4)}x${Math.round(cbox.h / 4)}` : 'none';
	return { imagePath, W, H, screen: { w: maxx - minx + 1, h: maxy - miny + 1 },
		cubePx, framePx, bgPx, shades: [...shades].sort(), cbox, sig };
}

let failures = 0;
const check = (ok, msg) => { console.log(`  ${ok ? 'OK  ' : 'FAIL'} ${msg}`); if (!ok) failures++; };

const image = arg('--image');
const seqDir = arg('--sequence-dir');

if (image) {
	const s = analyze(image);
	console.log(`verify-math3d-cube: ${path.basename(image)} (${s.W}x${s.H})`);
	check(s.screen.w > 200 && s.screen.h > 150, `pantalla detectada (${s.screen.w}x${s.screen.h})`);
	check(s.bgPx > 1000, `fondo presente (${s.bgPx} px)`);
	check(s.framePx > 500, `marco fijo presente (${s.framePx} px)`);
	check(s.cubePx > 100, `cubo presente (${s.cubePx} px, caja ${s.cbox.w}x${s.cbox.h})`);
} else if (seqDir) {
	if (!fs.existsSync(seqDir)) { console.error(`No existe ${seqDir}`); process.exit(2); }
	const frames = fs.readdirSync(seqDir).filter((f) => /^frame_\d+\.png$/.test(f)).sort();
	console.log(`verify-math3d-cube: secuencia ${path.basename(seqDir)} (${frames.length} frames)`);
	check(frames.length >= 4, `frames suficientes (${frames.length})`);
	const results = frames.map((f) => analyze(path.join(seqDir, f)));
	for (const r of results) {
		console.log(`    ${path.basename(r.imagePath)}: cubo=${r.cubePx} marco=${r.framePx} caja=${r.cbox.w}x${r.cbox.h} sig=${r.sig} sombras=[${r.shades}]`);
	}
	const good = results.filter((r) => r.cubePx >= 100);
	check(good.length >= 3, `frames con cubo visible (${good.length}/${results.length})`);
	check(results.every((r) => r.framePx > 500), 'marco presente en todos los frames');
	const sigs = new Set(good.map((r) => r.sig));
	check(sigs.size >= 2, `el cubo cambia entre frames (${sigs.size} firmas distintas)`);
	const multiShade = good.filter((r) => r.shades.length >= 2);
	check(multiShade.length >= 1, `sombreado por profundidad (${multiShade.length} frames con >=2 niveles)`);
} else {
	console.error('Uso: verify-math3d-cube.mjs --image <png> | --sequence-dir <dir>');
	process.exit(2);
}

if (failures === 0) {
	console.log('OK: demo 077 verificada (marco + cubo 3D animado).');
	process.exit(0);
}
console.log(`FAIL: ${failures} comprobacion(es) fallaron`);
process.exit(1);
