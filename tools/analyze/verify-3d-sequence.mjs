#!/usr/bin/env node
// ---------------------------------------------------------------------------
// Check de secuencia 3D on-target (Blitter): continuidad, ausencia de frames
// vacios/tearing grueso y, opcionalmente, movimiento del objeto.
//
// Uso:
//   node tools/analyze/verify-3d-sequence.mjs <dir-secuencia> [--min-frames N]
//        [--require-motion] [--bg 0,0,0] [--top 24] [--bottom 208]
//
// No conoce el modelo visual: separa el HUD (top) y la franja (bottom) y trabaja
// sobre la mascara de pixeles no-fondo del area 3D.
// ---------------------------------------------------------------------------
import fs from 'node:fs';
import path from 'node:path';
import { PNG } from 'pngjs';

const args = process.argv.slice(2);
const seqDir = args[0];
let minFrames = 3;
let requireMotion = false;
let bg = [0, 0, 0];
let top = 24;
let bottom = 0;
for (let i = 1; i < args.length; ++i) {
	const a = args[i];
	if (a === '--min-frames') minFrames = Math.max(1, parseInt(args[++i], 10));
	else if (a === '--require-motion') requireMotion = true;
	else if (a === '--bg') bg = args[++i].split(',').map((v) => parseInt(v, 10));
	else if (a === '--top') top = parseInt(args[++i], 10);
	else if (a === '--bottom') bottom = parseInt(args[++i], 10);
}
if (!seqDir || !fs.existsSync(seqDir)) {
	console.error('Uso: verify-3d-sequence.mjs <dir-secuencia> [--min-frames N] [--require-motion]');
	process.exit(2);
}

const files = fs.readdirSync(seqDir).filter((f) => /^frame_\d+\.png$/.test(f)).sort();
if (files.length < minFrames) {
	console.error(`[3d-seq] FAIL: ${files.length} frames; minimo ${minFrames}`);
	process.exit(1);
}

const frames = files.map((name) => {
	const png = PNG.sync.read(fs.readFileSync(path.join(seqDir, name)));
	return { name, png };
});
const { width, height } = frames[0].png;
if (bottom <= 0 || bottom > height) bottom = height - 24;
let failures = 0;
const fail = (msg) => {
	console.error(`[3d-seq] FAIL: ${msg}`);
	++failures;
};
for (const f of frames) {
	if (f.png.width !== width || f.png.height !== height) fail(`${f.name}: resolucion distinta`);
}
const masks = frames.map((f) => {
	const mask = new Uint8Array(width * height);
	const p = f.png.data;
	for (let y = top; y < Math.min(bottom, height); ++y) {
		for (let x = 0; x < width; ++x) {
			const i = (y * width + x) * 4;
			if (Math.abs(p[i] - bg[0]) + Math.abs(p[i + 1] - bg[1]) + Math.abs(p[i + 2] - bg[2]) > 24) {
				mask[y * width + x] = 1;
			}
		}
	}
	return mask;
});
const boxes = masks.map((mask) => {
	let x0 = width, y0 = height, x1 = -1, y1 = -1, area = 0;
	for (let y = top; y < Math.min(bottom, height); ++y) {
		for (let x = 0; x < width; ++x) {
			if (!mask[y * width + x]) continue;
			++area;
			if (x < x0) x0 = x;
			if (x > x1) x1 = x;
			if (y < y0) y0 = y;
			if (y > y1) y1 = y;
		}
	}
	return { x0, y0, x1, y1, area };
});
for (let i = 0; i < boxes.length; ++i) {
	const b = boxes[i];
	if (b.area < 32) fail(`${frames[i].name}: area 3D casi vacia (${b.area})`);
	if (i === 0) continue;
	const prev = boxes[i - 1];
	const moved = b.x0 !== prev.x0 || b.y0 !== prev.y0 || b.x1 !== prev.x1 || b.y1 !== prev.y1;
	if (!moved && !requireMotion && b.area === prev.area) continue;
	// Tearing/flicker grueso: el area no puede caer a menos del 20% ni multiplicarse
	// por 8 entre frames consecutivos del area 3D.
	if (b.area < prev.area / 5 || b.area > prev.area * 8) {
		fail(`${frames[i].name}: salto de area ${prev.area} -> ${b.area}`);
	}
}
if (requireMotion) {
	const first = boxes[0];
	const last = boxes[boxes.length - 1];
	const moved = first.x0 !== last.x0 || first.y0 !== last.y0 || first.x1 !== last.x1 ||
		      first.y1 !== last.y1 || first.area !== last.area;
	if (!moved) fail('sin movimiento entre primero y ultimo frame');
}
if (failures) process.exit(1);
console.log(`[3d-seq] OK: ${frames.length} frames ${width}x${height}; areas=${boxes.map((b) => b.area).join(',')}`);
