#!/usr/bin/env node
/**
 * Verificador visual de la demo 301 (`301_gui_layouts`).
 *
 * Comprueba, sobre la paleta EHB de la demo, que la UI con layouts se ha pintado:
 *  - relleno de panel (0x248) cubre varias zonas (los 4 paneles);
 *  - hay texto (0xfff) en varias zonas (etiquetas, botones, muestras de fuente, texto ajustado);
 *  - hay bisel claro (0xeee).
 * No exige overlay ni animacion (la demo es estatica).
 *
 * Uso:
 *   node tools/analyze/verify-gui-layouts.mjs --image <screenshot.png>
 *   node tools/analyze/verify-gui-layouts.mjs --sequence-dir <dir>
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

const COLORS = { fill: 0x248, text: 0xfff, shine: 0xeee, active: 0x46a };

/// Cuenta colores y los reparte por cuadrantes (para confirmar varias zonas con contenido).
function count(imagePath) {
	const img = readPng(imagePath);
	const W = img.width, H = img.height;
	const n = { fill: 0, text: 0, shine: 0, active: 0 };
	const textQ = [0, 0, 0, 0]; // texto por cuadrante (arriba-izq, arriba-der, abajo-izq, abajo-der)
	for (let y = 0; y < H; y++) {
		for (let x = 0; x < W; x++) {
			const [r, g, b] = pixel(img, x, y);
			let isText = false;
			for (const k in COLORS) {
				if (eq(r, g, b, COLORS[k])) { n[k]++; if (k === 'text') isText = true; }
			}
			if (isText) {
				const q = (y < H / 2 ? 0 : 2) + (x < W / 2 ? 0 : 1);
				textQ[q]++;
			}
		}
	}
	return { imagePath, W, H, ...n, textQ };
}

let failures = 0;
const check = (ok, msg) => { console.log(`  ${ok ? 'OK  ' : 'FAIL'} ${msg}`); if (!ok) failures++; };

const image = arg('--image');
const seqDir = arg('--sequence-dir');
const target = image || (seqDir ? path.join(seqDir, fs.readdirSync(seqDir).filter((f) => /^frame_\d{3}\.png$/.test(f)).sort()[0]) : '');

if (!target || !fs.existsSync(target)) {
	console.error('Uso: verify-gui-layouts.mjs --image <png> | --sequence-dir <dir>');
	process.exit(2);
}

const s = count(target);
console.log(`verify-gui-layouts: ${path.basename(target)} (${s.W}x${s.H})`);
check(s.fill > 5000, `relleno de paneles presente (${s.fill} px)`);
check(s.text > 1500, `texto presente (${s.text} px)`);
check(s.shine > 500, `bisel claro presente (${s.shine} px)`);
// Contenido en al menos 3 de los 4 cuadrantes (layouts repartidos por la pantalla).
const qs = s.textQ.filter((v) => v > 100).length;
check(qs >= 3, `texto repartido en >=3 cuadrantes (${qs}/4)`);

if (failures === 0) {
	console.log('OK: demo 301 (layouts + fuentes) verificada.');
	process.exit(0);
}
console.log(`FAIL: ${failures} comprobacion(es).`);
process.exit(1);
