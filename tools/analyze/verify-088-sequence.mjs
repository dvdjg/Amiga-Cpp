#!/usr/bin/env node
// Assertions over the 088 full-window PNG sequence: animation, edge/corner coverage,
// exact palette presence, and HUD isolation. Pair with run-demo's frame-step mode for
// transitions, and with the interval sequence for a full path coverage sweep.
import fs from 'node:fs';
import path from 'node:path';
import { createRequire } from 'node:module';

const require = createRequire(import.meta.url);
const { PNG } = require('pngjs');
const dir = process.argv[2];
const args = process.argv.slice(3);
const has = (flag) => args.includes(flag);
const intArg = (flag, fallback) => {
	const at = args.indexOf(flag);
	return at >= 0 ? Number.parseInt(args[at + 1] ?? '', 10) : fallback;
};
if (!dir || !fs.existsSync(dir)) {
	console.error('Uso: node tools/analyze/verify-088-sequence.mjs <dir-secuencia> [--consecutive] [--require-coverage]');
	process.exit(2);
}

const files = fs.readdirSync(dir)
	.filter((name) => /^frame_\d{3,}(?:_f\d+)?\.png$/.test(name))
	.sort();
const minimumFrames = intArg('--min-frames', has('--require-coverage') ? 24 : 8);
const requireCoverage = has('--require-coverage');
const requireConsecutive = has('--consecutive');
if (files.length < minimumFrames) {
	console.error(`[088-sequence] FAIL: ${files.length} frames; se requieren ${minimumFrames}`);
	process.exit(1);
}

const frames = files.map((name) => {
	const match = /_f(\d+)\.png$/.exec(name);
	return { name, frame: match ? Number(match[1]) : null, png: PNG.sync.read(fs.readFileSync(path.join(dir, name))) };
});
const width = frames[0].png.width;
const height = frames[0].png.height;
const scale = Math.min(Math.floor(width / 320), Math.floor(height / 256));
if (scale < 1 || frames.some(({ png }) => png.width !== width || png.height !== height)) {
	console.error('[088-sequence] FAIL: resolución incompatible o distinta entre capturas');
	process.exit(1);
}
const screenWidth = 320 * scale;
const screenHeight = 256 * scale;
// WinUAE's monitor capture includes the OCS display-window inset in addition to the
// centered client border; callers can override either origin for a different capture path.
const screenX = intArg('--screen-x', Math.floor((width - screenWidth) / 2) + 18);
const screenY = intArg('--screen-y', Math.floor((height - screenHeight) / 2) + 4);
const worldBottom = screenY + 232 * scale;
const tolerance = 3 * scale;
console.log(`[088-sequence] viewport=${width}x${height} screen=${screenX},${screenY} ${screenWidth}x${screenHeight} worldBottom=${worldBottom}`);
const corners = { upperLeft: false, upperRight: false, lowerLeft: false, lowerRight: false };
const edges = { left: false, right: false, top: false, bottom: false };
const colors = { red: 0, blue: 0, magenta: 0 };
let failures = 0;
const fail = (message) => { console.error(`[088-sequence] FAIL: ${message}`); ++failures; };

function pixelKind(data, offset) {
	const r = data[offset], g = data[offset + 1], b = data[offset + 2];
	if (r > 200 && g < 48 && b < 48) return 'red';
	if (b > 200 && r < 48 && g < 48) return 'blue';
	if (r > 200 && b > 200 && g < 48) return 'magenta';
	return null;
}

let previous = null;
for (let fi = 0; fi < frames.length; ++fi) {
	const { name, frame, png } = frames[fi];
	if (requireConsecutive) {
		if (frame === null) fail(`${name}: falta el frame real en el nombre`);
		if (fi > 0 && (frame === null || frames[fi - 1].frame === null || frame !== frames[fi - 1].frame + 1)) {
			fail(`${name}: los frames no son consecutivos (${frames[fi - 1].frame} → ${frame})`);
		}
	}
	let minX = screenX + screenWidth, minY = worldBottom;
	let maxX = screenX - 1, maxY = screenY - 1;
	let worldCount = 0, hudText = 0, hudForeign = 0, changed = 0;
	let hudForeignMinX = width, hudForeignMinY = height, hudForeignMaxX = -1, hudForeignMaxY = -1;
	const frameCorners = { upperLeft: false, upperRight: false, lowerLeft: false, lowerRight: false };
	for (let y = screenY; y < screenY + screenHeight; ++y) {
		for (let x = screenX; x < screenX + screenWidth; ++x) {
			const offset = (y * width + x) * 4;
			const kind = pixelKind(png.data, offset);
			if (y >= worldBottom) {
				if (kind === 'red' || kind === 'blue') {
					++hudForeign;
					if (x < hudForeignMinX) hudForeignMinX = x;
					if (x > hudForeignMaxX) hudForeignMaxX = x;
					if (y < hudForeignMinY) hudForeignMinY = y;
					if (y > hudForeignMaxY) hudForeignMaxY = y;
				}
				if (kind === 'magenta') ++hudText;
				continue;
			}
			if (previous) {
				const oldOffset = (y * width + x) * 4;
				if (png.data[offset] !== previous.data[oldOffset] ||
				    png.data[offset + 1] !== previous.data[oldOffset + 1] ||
				    png.data[offset + 2] !== previous.data[oldOffset + 2]) ++changed;
			}
			if (!kind) continue;
			++worldCount;
			++colors[kind];
			if (x < minX) minX = x;
			if (x > maxX) maxX = x;
			if (y < minY) minY = y;
			if (y > maxY) maxY = y;
			const left = x <= screenX + tolerance;
			const right = x >= screenX + screenWidth - 1 - tolerance;
			const top = y <= screenY + tolerance;
			const bottom = y >= worldBottom - 1 - tolerance;
			frameCorners.upperLeft ||= left && top;
			frameCorners.upperRight ||= right && top;
			frameCorners.lowerLeft ||= left && bottom;
			frameCorners.lowerRight ||= right && bottom;
		}
	}
	if (worldCount === 0) fail(`${name}: no aparece geometría de color en el viewport`);
	if (hudText === 0) fail(`${name}: no hay texto magenta en la banda HUD`);
	if (hudForeign !== 0) fail(`${name}: ${hudForeign} píxeles rojos/azules invaden el HUD`);
	if (requireConsecutive && fi > 0 && changed === 0) fail(`${name}: frame idéntico al anterior`);
	if (worldCount > 0) {
		edges.left ||= minX <= screenX + tolerance;
		edges.right ||= maxX >= screenX + screenWidth - 1 - tolerance;
		edges.top ||= minY <= screenY + tolerance;
		edges.bottom ||= maxY >= worldBottom - 1 - tolerance;
		for (const corner of Object.keys(corners)) corners[corner] ||= frameCorners[corner];
	}
	const foreignBox = hudForeign ? `${hudForeignMinX},${hudForeignMinY}..${hudForeignMaxX},${hudForeignMaxY}` : '-';
	console.log(`${name} frame=${frame ?? '-'} geometry=${worldCount} HUD-magenta=${hudText} HUD-foreign=${hudForeign} box=${foreignBox} diff=${fi ? changed : '-'}`);
	previous = png;
}

if (requireCoverage) {
	for (const [edge, seen] of Object.entries(edges)) if (!seen) fail(`la secuencia no alcanza el borde ${edge}`);
	for (const [corner, seen] of Object.entries(corners)) if (!seen) fail(`la secuencia no cruza la esquina ${corner}`);
	for (const color of ['red', 'blue', 'magenta']) if (colors[color] === 0) fail(`no aparece el color ${color}`);
}
if (failures) process.exit(1);
console.log(`[088-sequence] OK: ${files.length} frames, resolución ${width}x${height}, escala ${scale}x; edges=${JSON.stringify(edges)} corners=${JSON.stringify(corners)} colors=${JSON.stringify(colors)}`);
