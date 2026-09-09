#!/usr/bin/env node
/**
 * Comprobador visual de la demo 053 (multiplexado de sprites + color multiplexing).
 *
 * La demo dibuja SEIS barras de 16 px (anchura decreciente) en UN canal de sprite,
 * cada una con un tono saturado distinto (rojo, verde, azul, amarillo, cian,
 * magenta), apiladas verticalmente. Valida:
 *   1. Los seis tonos saturados están presentes (cantidad mínima de píxeles).
 *   2. Cada tono ocupa una franja vertical distinta, en orden creciente (eso
 *      demuestra el reuso vertical del canal, no un único bloque de color).
 *
 * Uso: node dist/tools/analyze/verify_sprite_multiplex.js --image <png>
 */
import * as fs from 'fs';
import * as path from 'path';
import { argValue, fail } from '../lib/cli.js';
import { readPng } from '../lib/image.js';

function classify(r: number, g: number, b: number): string | null {
	const max = Math.max(r, g, b);
	if (max < 128) return null; // fondo navy y negros: demasiado oscuros
	const t = 2;
	if (r >= g * t && r >= b * t) return 'red';
	if (g >= r * t && g >= b * t) return 'green';
	if (b >= r * t && b >= g * t) return 'blue';
	if (r >= b * t && g >= b * t) return 'yellow';
	if (g >= r * t && b >= r * t) return 'cyan';
	if (r >= g * t && b >= g * t) return 'magenta';
	return null;
}

function main() {
	const args = process.argv.slice(2);
	const imagePath = argValue(args, '--image');
	if (!imagePath) fail('Uso: verify_sprite_multiplex.js --image <png>');
	const resolved = path.resolve(imagePath);
	if (!fs.existsSync(resolved)) fail(`No existe: ${resolved}`);

	const img = readPng(resolved);
	const hues = ['red', 'green', 'blue', 'yellow', 'cyan', 'magenta'];
	const count: Record<string, number> = Object.fromEntries(hues.map((h) => [h, 0]));
	const sumY: Record<string, number> = Object.fromEntries(hues.map((h) => [h, 0]));

	for (let y = 0; y < img.height; y++) {
		for (let x = 0; x < img.width; x++) {
			const i = (y * img.width + x) * 4;
			const c = classify(img.data[i], img.data[i + 1], img.data[i + 2]);
			if (c && c in count) {
				count[c]++;
				sumY[c] += y;
			}
		}
	}

	const minCount = 20;
	for (const h of hues) {
		if (count[h] < minCount) {
			fail(`FAIL sprite: tono ${h} ausente o escaso (${count[h]} px)`);
		}
	}

	// Las seis franjas deben estar apiladas en orden vertical creciente.
	const centroids = hues.map((h) => sumY[h] / Math.max(1, count[h]));
	for (let i = 1; i < hues.length; i++) {
		if (!(centroids[i] > centroids[i - 1])) {
			fail(
				`FAIL sprite: franjas no ordenadas verticalmente (${hues[i]}@${centroids[i].toFixed(0)} no va después de ${hues[i - 1]}@${centroids[i - 1].toFixed(0)})`
			);
		}
	}

	const summary = hues.map((h) => `${h}=${count[h]}`).join(' ');
	console.log(`OK sprite: ${summary} | y=${centroids.map((c) => c.toFixed(0)).join(',')}`);
}

main();
