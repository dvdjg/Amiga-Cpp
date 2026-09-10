#!/usr/bin/env node
/**
 * Comprobador visual de la demo 054 (SpriteAllocator: asignación de canales).
 *
 * La demo dibuja 9 sprites de 16×16 en una fila horizontal; los 8 primeros caben
 * en los canales 0..7 (parejas de color rojo, verde, azul, amarillo) y el noveno
 * desborda a BOB. Valida:
 *   1. Los 4 colores de las parejas están presentes.
 *   2. Están ordenados de izquierda a derecha (rojo < verde < azul < amarillo),
 *      lo que confirma la asignación secuencial de canales del allocator.
 *
 * Uso: node dist/tools/analyze/verify_sprite_allocator.js --image <png>
 */
import * as fs from 'fs';
import * as path from 'path';
import { argValue, fail } from '../lib/cli.js';
import { readPng } from '../lib/image.js';

function main() {
	const args = process.argv.slice(2);
	const imagePath = argValue(args, '--image');
	if (!imagePath) fail('Uso: verify_sprite_allocator.js --image <png>');
	const resolved = path.resolve(imagePath);
	if (!fs.existsSync(resolved)) fail(`No existe: ${resolved}`);

	const img = readPng(resolved);
	const colors = [
		['red', [255, 0, 0]],
		['green', [0, 255, 0]],
		['blue', [0, 0, 255]],
		['yellow', [255, 255, 0]],
	] as const;

	const count: Record<string, number> = {};
	const sumX: Record<string, number> = {};
	for (const [name] of colors) { count[name] = 0; sumX[name] = 0; }

	for (let y = 0; y < img.height; y++) {
		for (let x = 0; x < img.width; x++) {
			const i = (y * img.width + x) * 4;
			for (const [name, c] of colors) {
				if (img.data[i] === c[0] && img.data[i + 1] === c[1] && img.data[i + 2] === c[2]) {
					count[name]++;
					sumX[name] += x;
				}
			}
		}
	}

	const minCount = 40;
	for (const [name] of colors) {
		if (count[name] < minCount) {
			fail(`FAIL allocator: color ${name} ausente o escaso (${count[name]} px)`);
		}
	}

	// Orden horizontal: rojo < verde < azul < amarillo (canales 0..7 en orden).
	const centroids = colors.map(([name]) => sumX[name] / Math.max(1, count[name]));
	for (let i = 1; i < colors.length; i++) {
		if (!(centroids[i] > centroids[i - 1])) {
			fail(`FAIL allocator: colores no ordenados horizontalmente (${colors[i][0]} no va a la derecha de ${colors[i - 1][0]})`);
		}
	}

	const summary = colors.map(([name]) => `${name}=${count[name]}`).join(' ');
	console.log(`OK allocator: ${summary} | x=${centroids.map((c) => c.toFixed(0)).join(',')}`);
}

main();
