#!/usr/bin/env node
/**
 * Detector de negro interno en capturas o secuencias de frames.
 *
 * Sustituye a assert_no_inner_black.py (Pillow) por una version Node/pngjs.
 * Localiza el rectangulo no negro (borde de WinUAE) y comprueba que dentro de
 * la ventana visible no aparezcan manchas negras inesperadas (sintoma de un
 * reinicio de Copper o de un puntero de bitplane corrompido).
 *
 * Uso: node dist/tools/analyze/assert_no_inner_black.js <archivo|carpeta>
 *      [maxBlackRatio]
 */
import * as fs from 'fs';
import * as path from 'path';
import { readPng } from '../lib/image.js';
import { fail } from '../lib/cli.js';

interface Failure {
	name: string;
	kind: string;
	ratio: number;
	black: number;
	total: number;
}

/** Comprueba un frame y devuelve un fallo o null si pasa. */
function frameHasInnerBlack(framePath: string, maxBlackRatio: number): Failure | null {
	const image = readPng(framePath);
	const { width, height, data } = image;

	let xs: number[] = [];
	let ys: number[] = [];
	for (let y = 0; y < height; ++y) {
		for (let x = 0; x < width; ++x) {
			const i = (y * width + x) * 4;
			if (data[i] !== 0 || data[i + 1] !== 0 || data[i + 2] !== 0) {
				xs.push(x);
				ys.push(y);
			}
		}
	}
	if (xs.length === 0) {
		return { name: path.basename(framePath), kind: 'empty', ratio: 1.0, black: 0, total: 0 };
	}

	// min/max por bucle: Math.min(...xs) revienta la pila con frames grandes
	// (decenas de miles de pixeles no negros).
	let left = width, right = 0, top = height, bottom = 0;
	for (const x of xs) { if (x < left) left = x; if (x > right) right = x; }
	for (const y of ys) { if (y < top) top = y; if (y > bottom) bottom = y; }
	let black = 0;
	let total = 0;
	for (let y = top; y <= bottom; ++y) {
		for (let x = left; x <= right; ++x) {
			const i = (y * width + x) * 4;
			total++;
			if (data[i] === 0 && data[i + 1] === 0 && data[i + 2] === 0) black++;
		}
	}
	const ratio = total ? black / total : 1.0;
	if (ratio > maxBlackRatio) {
		return { name: path.basename(framePath), kind: 'inner-black', ratio, black, total };
	}
	return null;
}

function main(): void {
	const args = process.argv.slice(2);
	if (args.length < 1) {
		fail('Uso: assert_no_inner_black.js <archivo|carpeta> [maxBlackRatio]');
	}
	const source = path.resolve(args[0]);
	const maxBlackRatio = args.length > 1 ? parseFloat(args[1]) : 0.001;

	let frames: string[] = [];
	if (fs.existsSync(source) && fs.statSync(source).isDirectory()) {
		frames = fs.readdirSync(source)
			.filter((name) => name.startsWith('frame_') && name.endsWith('.png'))
			.sort()
			.map((name) => path.join(source, name));
	} else {
		frames = [source];
	}
	if (frames.length === 0) {
		fail(`No frame_*.png files found in ${source}`);
	}

	const failed: Failure[] = [];
	for (const frame of frames) {
		const failure = frameHasInnerBlack(frame, maxBlackRatio);
		if (failure !== null) failed.push(failure);
	}

	if (failed.length > 0) {
		for (const f of failed) {
			console.error(`${f.name}: ${f.kind} ratio=${f.ratio.toFixed(6)} black=${f.black} total=${f.total}`);
		}
		process.exit(1);
	}

	console.log(`OK ${frames.length} frame(s), max inner black ratio <= ${maxBlackRatio}`);
}

main();
