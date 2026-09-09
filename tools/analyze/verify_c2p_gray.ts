#!/usr/bin/env node
/**
 * Comprobador visual de la demo 061 (c2p chunky 4bpp -> planar).
 *
 * La demo dibuja una rampa de grises RGB444 `i*0x111` para los indices 0..15 en el
 * area chunky (160x128) arriba-izquierda, y el resto a negro. Este validador:
 *
 *   1. Comprueba que hay pixeles de (al menos) varios de los grises esperados
 *      (17,34,51,...255) en la zona superior de la captura.
 *   2. Exige que la mayoria de pixeles no-negros caigan en la rampa esperada
 *      (no colores inventados): si el c2p desintercala mal, habria grises "rotos"
 *      o indices fuera de la rampa.
 *
 * Uso: node dist/tools/analyze/verify_c2p_gray.js --image <png>
 */
import * as fs from 'fs';
import * as path from 'path';
import { argValue, fail } from '../lib/cli.js';
import { readPng } from '../lib/image.js';

/** Colores esperados de la rampa i*0x111 (RGB444 escalados a 8bpp). */
const GRAY_8 = [17, 34, 51, 68, 85, 102, 119, 136, 153, 170, 187, 204, 221, 238, 255];

interface Stats {
	total: number;
	ramp: number;
	nonBlack: number;
	present: number[];
}

function analyze(imagePath: string): Stats {
	const image = readPng(imagePath);
	const { width, height, data } = image;
	const ramp = new Set(GRAY_8);
	const stats: Stats = { total: 0, ramp: 0, nonBlack: 0, present: [] };
	const seen = new Set<number>();

	// Zona superior-izquierda: el buffer chunky de 160x128 px Amiga se muestra con
	// EHB 320x256; en la captura (756x576) ocupa aprox. x=40..380, y=36..290.
	const x0 = 36, x1 = Math.min(width, 392);
	const y0 = 36, y1 = Math.min(height, 300);
	const step = 2;

	for (let y = y0; y < y1; y += step) {
		for (let x = x0; x < x1; x += step) {
			const i = (y * width + x) * 4;
			const r = data[i], g = data[i + 1], b = data[i + 2];
			const isGray = r === g && g === b;
			if (r < 8 && g < 8 && b < 8) continue; // negro
			stats.total++;
			stats.nonBlack++;
			if (isGray && ramp.has(r)) {
				stats.ramp++;
				if (!seen.has(r)) {
					seen.add(r);
					stats.present.push(r);
				}
			}
		}
	}
	stats.present.sort((a, b) => a - b);
	return stats;
}

function main(): void {
	const args = process.argv.slice(2);
	const imagePath = argValue(args, '--image');
	if (!imagePath) {
		fail('Uso: verify_c2p_gray.js --image <png>');
	}
	const resolved = path.resolve(imagePath);
	if (!fs.existsSync(resolved)) {
		fail(`No existe la captura: ${resolved}`);
	}

	const s = analyze(resolved);

	// Criterios: al menos 8 tonos distintos de la rampa presentes y >= 85% de los
	// pixeles no-negros dentro de la rampa esperada.
	if (s.present.length < 8) {
		fail(`FAIL c2p: solo ${s.present.length} tonos de rampa (esperaba >=8)`);
	}
	if (s.nonBlack > 0 && s.ramp / s.nonBlack < 0.85) {
		fail(`FAIL c2p: solo ${s.ramp}/${s.nonBlack} pixeles en la rampa esperada`);
	}
	if (s.total === 0) {
		fail(`FAIL c2p: zona sin pixeles no-negros (nada renderizado)`);
	}

	console.log(`OK c2p: tonos=${s.present.length} rampa=${s.ramp}/${s.nonBlack} sample=${s.total}`);
}

main();