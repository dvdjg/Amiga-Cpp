#!/usr/bin/env node
/**
 * Verificador visual de la demo 079 (`079_wireframe`).
 *
 * La demo dibuja un alambre (malla `pilka`) sobre fondo oscuro con una paleta azul/cian
 * (sin blanco puro): se comprueba que hay "pantalla", fondo oscuro y píxeles del alambre
 * (brillantes). No usa el analizador genérico (que pide colores de overlay).
 *
 * Uso:
 *   node tools/analyze/verify-wireframe.mjs --image <screenshot.png>
 * Salida: informe por stdout; exit 0 = OK, 1 = fallo, 2 = uso.
 */
import * as fs from 'fs';
import * as path from 'path';
import { readPng, pixel } from '../../dist/tools/lib/image.js';

const arg = (name, def = '') => {
	const i = process.argv.indexOf(name);
	return i >= 0 && process.argv[i + 1] && !process.argv[i + 1].startsWith('--') ? process.argv[i + 1] : def;
};

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
	let bright = 0, dark = 0;
	for (let y = miny; y <= maxy; y++)
		for (let x = minx; x <= maxx; x++) {
			const [r, g, b] = pixel(img, x, y);
			const lum = r + g + b;
			if (lum > 0x180) bright++;         // alambre (colores claros de la paleta)
			else if (lum < 0x60) dark++;       // fondo (0x012)
		}
	return { imagePath, W, H, screen: { w: maxx - minx + 1, h: maxy - miny + 1 }, bright, dark };
}

let failures = 0;
const check = (ok, msg) => { console.log(`  ${ok ? 'OK  ' : 'FAIL'} ${msg}`); if (!ok) failures++; };

const image = arg('--image');
if (!image) {
	console.error('Uso: verify-wireframe.mjs --image <png>');
	process.exit(2);
}
if (!fs.existsSync(image)) {
	console.error(`No existe ${image}`);
	process.exit(2);
}

const s = analyze(image);
console.log(`verify-wireframe: ${path.basename(image)} (${s.W}x${s.H})`);
check(s.screen.w > 200 && s.screen.h > 150, `pantalla detectada (${s.screen.w}x${s.screen.h})`);
check(s.dark > 5000, `fondo oscuro presente (${s.dark} px)`);
check(s.bright > 200, `alambre presente (${s.bright} px brillantes)`);

if (failures === 0) {
	console.log('OK: demo 079 verificada (fondo + alambre).');
	process.exit(0);
}
console.log(`FAIL: ${failures} comprobacion(es) fallaron`);
process.exit(1);
