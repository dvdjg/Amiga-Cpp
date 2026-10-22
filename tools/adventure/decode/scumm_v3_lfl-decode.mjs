#!/usr/bin/env node
// Decodifica fondos FM-Towns de SCUMM V3 (.LFL) y costumes clásicos a PNG indexado.
// Formatos contrastados con ScummVM 2026.4.0: gfx.cpp (decompressBitmap/unkDecode8-11),
// palette.cpp (setPaletteFromPtr) y costume.cpp (ClassicCostumeLoader/loadCostume).

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { parseCostume, renderSheet } from './costume.mjs';
import { encodeIndexedPng, paletteTo8Bit } from '../lib/png.mjs';
import { writeDirReadme } from '../lib/docs.mjs';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const REPO = path.resolve(HERE, '../../..');

function readBitsLsb(src, start) {
	let p = start;
	let value = 0;
	let mask = 0;
	return {
		read(n) {
			let out = 0;
			for (let i = 0; i < n; i++) {
				if (mask === 0) {
					if (p >= src.length) throw new Error('tira truncada al leer bits');
					value = src[p++];
					mask = 1;
				}
				if (value & mask) out |= 1 << i;
				mask <<= 1;
				if (mask === 256) mask = 0;
			}
			return out;
		},
		get offset() { return p; },
	};
}

function putColumnMajor(indices, width, height, stripX, cursor, color) {
	if (cursor >= width * height) return;
	const x = stripX + Math.floor(cursor / height);
	const y = cursor % height;
	indices[y * width + x] = color & 0xff;
}

/** Decodifica una tira (8 × alto) de los códecs usados por Indy3 FM-Towns. */
export function decodeTownsStrip(src, width, height, stripX) {
	if (width !== 8 || height <= 0) throw new Error('tira FM-Towns: dimensiones inválidas');
	if (!src.length) throw new Error('tira FM-Towns vacía');
	const code = src[0];
	const pixels = new Uint8Array(width * height);
	let cursor = 0;
	const put = (color) => putColumnMajor(pixels, width, height, 0, cursor++, color);
	if (code === 1) {
		if (src.length < 1 + pixels.length) throw new Error('tira RAW256 truncada');
		for (let i = 0; i < pixels.length; i++) put(src[i + 1]);
	} else if (code === 2) {
		let p = 1;
		while (cursor < pixels.length) {
			if (p + 2 > src.length) throw new Error('tira TOWNS_2 truncada');
			const run = src[p++] + 1;
			const color = src[p++];
			for (let i = 0; i < run && cursor < pixels.length; i++) put(color);
		}
	} else if (code === 3) {
		const bits = readBitsLsb(src, 1);
		let bank = 0;
		while (cursor < pixels.length) {
			const op = bits.read(4);
			switch (op >>> 2) {
				case 0: {
					const color = bank * 16 + bits.read(4);
					for (let i = 0; i < (op & 3) + 2 && cursor < pixels.length; i++) put(color);
					break;
				}
				case 1:
					for (let i = 0; i < (op & 3) + 1 && cursor < pixels.length; i++) put(bank * 16 + bits.read(4));
					break;
				case 2:
					bank = bits.read(4);
					break;
				default:
					break;
			}
		}
	} else if (code === 4) {
		let p = 1;
		const numColors = src[p++];
		if (p + numColors > src.length) throw new Error('paleta local TOWNS_4 truncada');
		const palette = src.subarray(p, p + numColors);
		p += numColors;
		while (cursor < pixels.length) {
			if (p >= src.length) throw new Error('tira TOWNS_4 truncada');
			const op = src[p++];
			if (op < numColors) put(palette[op]);
			else {
				if (p >= src.length) throw new Error('run TOWNS_4 sin color');
				const run = op - numColors + 1;
				const color = src[p++];
				for (let i = 0; i < run && cursor < pixels.length; i++) put(color);
			}
		}
	} else if (code === 7) {
		if (src.length < 2) throw new Error('tira TOWNS_7 truncada');
		const bits = readBitsLsb(src, 2);
		let color = src[1];
		let inc = 1;
		while (cursor < pixels.length) {
			put(color);
			let n = 0;
			while (n < 3 && bits.read(1)) n++;
			if (n === 1) { inc = -inc; color = (color - inc) & 0xff; }
			else if (n === 2) color = (color - inc) & 0xff;
			else if (n === 3) { inc = 1; color = bits.read(8); }
		}
	} else {
		throw new Error(`código de tira FM-Towns no soportado: ${code}`);
	}
	return { indices: pixels, code };
}

/** Decodifica el bloque BM: longitud u32LE, offsets de tira u32LE, payload por tira. */
export function decodeTownsBitmap(bitmap, width, height) {
	if (!bitmap || bitmap.length < 4 || width <= 0 || width % 8 !== 0 || height <= 0) throw new Error('BM: cabecera o dimensiones inválidas');
	const strips = width / 8;
	const dataSize = bitmap.readUInt32LE(0);
	if (dataSize > bitmap.length - 4 || 4 + dataSize > bitmap.length) throw new Error('BM: longitud fuera de rango');
	const pixels = new Uint8Array(width * height);
	const codes = [];
	for (let strip = 0; strip < strips; strip++) {
		const tableOffset = 4 + strip * 4;
		if (tableOffset + 4 > bitmap.length) throw new Error(`BM: falta el offset de tira ${strip}`);
		const offset = bitmap.readUInt32LE(tableOffset);
		if (offset < 4 + strips * 4 || offset >= 4 + dataSize) throw new Error(`BM: offset inválido en tira ${strip}: ${offset}`);
		const decoded = decodeTownsStrip(bitmap.subarray(offset, 4 + dataSize), 8, height, strip * 8);
		codes.push(decoded.code);
		for (let y = 0; y < height; y++) pixels.set(decoded.indices.subarray(y * 8, y * 8 + 8), y * width + strip * 8);
	}
	return { width, height, strips, codes: [...new Set(codes)], indices: pixels };
}

function parseArgs(argv) {
	const out = { _: [] };
	for (let i = 0; i < argv.length; i++) {
		const a = argv[i];
		if (a.startsWith('--')) out[a.slice(2)] = argv[++i];
		else out._.push(a);
	}
	return out;
}

function roomBlocks(ws, room) {
	const dir = path.join(ws, 'raw', 'rooms', `room_${room}`);
	const manifest = JSON.parse(fs.readFileSync(path.join(dir, 'blocks.json'), 'utf8'));
	return { dir, entries: manifest.blocks, load: (entry) => fs.readFileSync(path.join(dir, entry.file)) };
}

function main(target) {
	const ws = fs.existsSync(path.join(REPO, 'out', 'adventure', target)) ? path.join(REPO, 'out', 'adventure', target) : path.resolve(target);
	const game = JSON.parse(fs.readFileSync(path.join(ws, 'game.json'), 'utf8'));
	if (!game.engine.includes('v3') || !game.edition.toLowerCase().includes('towns')) throw new Error('Este decodificador requiere un workspace SCUMM V3 FM-Towns.');
	const index = JSON.parse(fs.readFileSync(path.join(ws, 'raw', 'index', 'lfl.json'), 'utf8'));
	const pngRoot = path.join(ws, 'decoded', 'png');
	const roomsDir = path.join(pngRoot, 'rooms');
	const costumesDir = path.join(pngRoot, 'costumes');
	fs.mkdirSync(roomsDir, { recursive: true });
	fs.mkdirSync(costumesDir, { recursive: true });
	const manifest = { format: 'adv-v3-lfl-images/1', game: game.id, rooms: [], costumes: [], errors: [] };
	const paletteByRoom = new Map();
	for (const room of fs.readdirSync(path.join(ws, 'raw', 'rooms')).filter((n) => /^room_\d+$/.test(n)).map((n) => Number(n.slice(5))).sort((a, b) => a - b)) {
		const { entries, load } = roomBlocks(ws, room);
		const find = (tag) => entries.find((e) => e.tag === tag);
		try {
			const hdEntry = find('HD');
			const paEntry = find('PA');
			const bmEntry = find('BM');
			if (!hdEntry || !paEntry || !bmEntry) throw new Error('faltan bloques HD, PA o BM');
			const hd = load(hdEntry);
			const width = hd.readUInt16LE(0);
			const height = hd.readUInt16LE(2);
			const pa = load(paEntry);
			const numColors = pa.readUInt16LE(0);
			if (numColors < 1 || numColors > 256 || pa.length < 2 + numColors * 3) throw new Error(`PA inválido: ${numColors} colores en ${pa.length} bytes`);
			const palette = [];
			for (let i = 0; i < numColors; i++) palette.push([pa[2 + i * 3], pa[3 + i * 3], pa[4 + i * 3]]);
			const image = decodeTownsBitmap(load(bmEntry), width, height);
			const file = `rooms/room_${room}.png`;
			fs.writeFileSync(path.join(pngRoot, file), encodeIndexedPng(width, height, image.indices, paletteTo8Bit(palette)));
			paletteByRoom.set(room, paletteTo8Bit(palette));
			manifest.rooms.push({ room, file, width, height, strips: image.strips, codes: image.codes });
		} catch (error) {
			manifest.errors.push({ kind: 'room', room, message: error.message });
		}
	}

	const costumeLocations = index.tables.costumes ?? [];
	const seen = new Set();
	for (let id = 1; id < costumeLocations.length; id++) {
		const location = costumeLocations[id];
		if (!location?.roomno || !location.off) continue;
		const room = location.roomno;
		const key = `${room}:${location.off}`;
		if (seen.has(key)) continue;
		seen.add(key);
		try {
			const { entries, load } = roomBlocks(ws, room);
			const entry = entries.find((e) => e.tag === 'CO' && e.off === location.off);
			if (!entry) throw new Error(`no se encuentra bloque CO en sala ${room}, offset ${location.off}`);
			const costume = parseCostume(load(entry), { useAnimationCommands: true });
			if (!costume.cels.length) throw new Error('costume sin cels decodificables');
			const sheet = renderSheet(costume);
			const base = `costume_${String(id).padStart(3, '0')}`;
			const palette = paletteByRoom.get(room);
			if (!palette) throw new Error(`paleta de sala ${room} no disponible`);
			const files = [];
			if (sheet.W * sheet.H <= 8_000_000) {
				const file = `${base}.png`;
				fs.writeFileSync(path.join(costumesDir, file), encodeIndexedPng(sheet.W, sheet.H, sheet.pix, palette, { transparentIndex: 0 }));
				files.push({ file, width: sheet.W, height: sheet.H });
			} else {
				for (const limb of costume.limbs) {
					const part = { ...costume, limbs: [limb], cels: costume.cels.filter((cel) => cel.limb === limb.limb) };
					const limbSheet = renderSheet(part);
					const file = `${base}_limb${String(limb.limb).padStart(2, '0')}.png`;
					fs.writeFileSync(path.join(costumesDir, file), encodeIndexedPng(limbSheet.W, limbSheet.H, limbSheet.pix, palette, { transparentIndex: 0 }));
					files.push({ file, width: limbSheet.W, height: limbSheet.H, limb: limb.limb });
				}
			}
			manifest.costumes.push({ id, room, format: `0x${costume.format.toString(16)}`, palette: costume.palette, limbs: costume.limbs, celCount: costume.cels.length, skippedCels: costume.skippedCels, files });
		} catch (error) {
			manifest.errors.push({ kind: 'costume', id, room, offset: location.off, message: error.message });
		}
	}

	fs.writeFileSync(path.join(pngRoot, 'images.json'), JSON.stringify(manifest, null, 2) + '\n');
	writeDirReadme(pngRoot, {
		title: 'Imágenes normalizadas de Indy3 FM-Towns',
		what: 'Fondos SCUMM V3 FM-Towns y costumes clásicos decodificados a PNG indexado sin pérdida de índices, con paletas locales.',
		producedBy: '`node tools/adventure/decode/scumm_v3_lfl-decode.mjs indy3`.',
		consumedBy: 'revisión visual, previewer y futura cocina de assets para el port.',
		items: [
			{ name: 'rooms/room_<N>.png', desc: 'fondo decodificado del bloque BM de la sala N.' },
			{ name: 'costumes/costume_<N>.png', desc: 'rejilla de cels por limb/frame; costumes grandes se dividen por limb.' },
			{ name: 'images.json', desc: 'manifiesto de cobertura, dimensiones, codecs y errores.' },
		],
		related: [{ name: '`../../raw/`', desc: 'bloques `.LFL` originales exportados por el frontend V3/V4.' }],
	});
	console.log(`[v3-decode] ${game.id}: ${manifest.rooms.length} fondos, ${manifest.costumes.length} costumes, ${manifest.errors.length} errores -> ${pngRoot}`);
}

const target = parseArgs(process.argv.slice(2))._[0];
const isMain = process.argv[1] !== undefined && import.meta.url === pathToFileURL(process.argv[1]).href;
if (isMain) {
	if (!target) {
		console.error('Uso: node tools/adventure/decode/scumm_v3_lfl-decode.mjs <juego|dir>');
		process.exit(2);
	}
	try { main(target); } catch (error) { console.error(`[v3-decode] ${error.message}`); process.exit(1); }
}
