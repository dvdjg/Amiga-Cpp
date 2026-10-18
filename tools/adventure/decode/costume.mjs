#!/usr/bin/env node
// Costumes `COST` (SCUMM V5) → **hoja PNG por costume** + manifiesto de cels. El decodificador
// es el port fiel del codec del original (`base-costume.cpp:byleRLEDecode`): por columna, runs
// de nibble `(color<<4)|len` (len 0 → longitud en el byte siguiente), `color 0` transparente,
// dibujo **column-major** (y avanza por fila de pantalla hasta completar la altura de la
// columna). Cabecera de costume (`ClassicCostumeLoader::loadCostume`, costume.cpp:417): el
// puntero del recurso apunta al **tag** (`COST`+tamaño; resource.cpp:757) y `base = ptr + 2`,
// así que sobre el payload (bytes tras el tag) `base = payload - 6`: `numAnim = base[6]`,
// `format = base[7] & 0x7F` (0x58/0x59), `palette = base+8` (`numColors` bytes = **índices de
// la paleta de sala**), tabla de frames por limb en `base+8+numColors+2` (16 limbs u16LE),
// tabla de datos de animación en `base+8+numColors+34`, `animCmds` en `base+u16LE(base+8+
// numColors)`. El cel es el `CostumeInfo` de 12 bytes (`w u16,h u16,relX i16,relY i16,moveX
// i16,moveY i16`) + RLE, con los offsets de frame relativos a `base`.
//
// Uso: node tools/adventure/decode/costume.mjs <juego> [--costume N] [--out dir]

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { encodeIndexedPng } from '../lib/png.mjs';
import { writeDirReadme } from '../lib/docs.mjs';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const REPO = path.resolve(HERE, '../../..');

function parseArgs(argv) {
	const out = { _: [] };
	for (let i = 0; i < argv.length; i++) {
		const a = argv[i];
		if (a.startsWith('--')) out[a.slice(2)] = argv[++i];
		else out._.push(a);
	}
	return out;
}

// --- decodificación de un cel -----------------------------------------------------------

/** Decodifica el RLE column-major de un cel (`byleRLEDecode`, sin escalado ni sombra).
 * \param cel puntero al `CostumeInfo` dentro del payload.
 * \return píxeles por filas (`w*h`, índices de color de costume; 0 = transparente). */
export function decodeCel(payload, celOff) {
	const w = payload.readUInt16LE(celOff);
	const h = payload.readUInt16LE(celOff + 2);
	const meta = {
		w, h,
		relX: payload.readInt16LE(celOff + 4),
		relY: payload.readInt16LE(celOff + 6),
		moveX: payload.readInt16LE(celOff + 8),
		moveY: payload.readInt16LE(celOff + 10),
	};
	const pix = new Uint8Array(w * h);
	if (!w || !h) return { ...meta, pix };
	const src = payload.subarray(celOff + 12);
	let i = 0;
	let x = 0;
	let y = 0;
	let height = h;
	let budget = 1 << 20; // tope de seguridad: datos corruptos no deben colgar el pipeline
	while (true) {
		if (i >= src.length) throw new Error('RLE de cel truncado');
		let len = src[i++];
		const color = len >> 4;
		len &= 0x0f;
		if (!len) len = src[i++];
		// Copia del bucle de `ByleRLEDecode_Mode0` (base-costume-optimised.cpp:270): un run se
		// parte en tantos tramos como columnas cruce; `height` cuenta **píxeles** de la columna.
		while (len > 0) {
			const batch = Math.min(height, len);
			len -= batch;
			height -= batch;
			if (color && x < w) {
				for (let b = 0; b < batch; b++) pix[(y + b) * w + x] = color;
			}
			y += batch;
			if (!budget--) throw new Error('RLE de cel desbordado (datos corruptos)');
			if (height === 0) {
				x++;
				if (x >= w) return { ...meta, pix };
				height = h;
				y = 0;
			}
		}
	}
	return { ...meta, pix };
}

/** Parsea un payload `COST` v5 (bytes tras `COST`+tamaño): cabecera, paleta e inventario de
 * cels por limb. `loadCostume` opera sobre el **tag**: `base = tag + 2`, y el tag está 8 bytes
 * antes del payload (resource.cpp:757 carga desde el tag), de modo que `base = payload - 6`
 * (se reconstruye rellenando 6 bytes; `base[0..5]` no se usan). */
export function parseCostume(payload) {
	const base = Buffer.concat([Buffer.alloc(6), payload]);
	const format = base[7] & 0x7f;
	const mirror = (base[7] & 0x80) !== 0;
	if (format !== 0x58 && format !== 0x59) throw new Error(`formato de costume no soportado 0x${format.toString(16)}`);
	const numColors = format === 0x59 ? 32 : 16;
	const palette = [...base.subarray(8, 8 + numColors)];
	const animCmds = base.readUInt16LE(8 + numColors);
	const frameOffs = [];
	const dataOffs = [];
	for (let l = 0; l < 16; l++) {
		frameOffs.push(base.readUInt16LE(8 + numColors + 2 + l * 2));
		dataOffs.push(base.readUInt16LE(8 + numColors + 34 + l * 2));
	}
	const bounds = [...frameOffs, ...dataOffs, animCmds].filter((o) => o > 2).sort((a, b) => a - b);
	const limbs = [];
	const cels = [];
	let skippedCels = 0;
	for (let limb = 0; limb < 16; limb++) {
		const o = frameOffs[limb];
		if (o <= 2) continue; // limb sin frames
		// La tabla de frames de un limb termina donde empieza el siguiente bloque físico
		// (tablas de frames, datos de animación o animCmds). Sin bloque posterior no hay
		// tabla: los offsets repetidos/colgantes del final del fichero no son frames.
		const next = bounds.find((b) => b > o);
		if (next === undefined) continue;
		const frames = [];
		for (let f = 0; f < Math.floor((next - o) / 2); f++) {
			const celOff = base.readUInt16LE(o + f * 2);
			if (!celOff) continue; // frame vacío (entrada 0 de la tabla)
			if (celOff + 12 > base.length) continue;
			const w = base.readUInt16LE(celOff);
			if (w > 320 || w === 0) continue; // cabecera de cel implausible: no se dibuja
			let cel;
			try {
				cel = decodeCel(base, celOff);
			} catch {
				// Entrada no decodificable (frames no referenciados por las animaciones pueden
				// apuntar a datos compartidos): se omite y se contabiliza, no tumba el costume.
				skippedCels++;
				continue;
			}
			cel.limb = limb;
			cel.frame = f;
			cel.celOff = celOff;
			cels.push(cel);
			frames.push(f);
		}
		if (frames.length) limbs.push({ limb, frames: frames.length });
	}
	return { format, mirror, numColors, palette, animCmds, limbs, cels, skippedCels };
}

// --- render y CLI ------------------------------------------------------------------------

/** Dibuja los cels en una rejilla (filas = limbs, columnas = frames) sobre índice 0. */
function renderSheet(costume) {
	const cellW = Math.max(...costume.cels.map((c) => c.w)) + 2;
	const cellH = Math.max(...costume.cels.map((c) => c.h)) + 2;
	const rows = costume.limbs.length;
	const cols = Math.max(...costume.limbs.map((l) => l.frames));
	const W = cols * cellW;
	const H = rows * cellH;
	const pix = new Uint8Array(W * H);
	costume.limbs.forEach((l, row) => {
		for (const c of costume.cels.filter((c) => c.limb === l.limb)) {
			const ox = c.frame * cellW + 1;
			const oy = row * cellH + 1;
			for (let y = 0; y < c.h; y++) {
				for (let x = 0; x < c.w; x++) {
					const v = c.pix[y * c.w + x];
					if (v) pix[(oy + y) * W + ox + x] = costume.palette[v] ?? v;
				}
			}
		}
	});
	return { W, H, pix, cellW, cellH, rows, cols };
}

const args = parseArgs(process.argv.slice(2));
const target = args._[0];
const isMain = process.argv[1] !== undefined && import.meta.url === pathToFileURL(process.argv[1]).href;
if (isMain && !target) {
	console.error('Uso: node tools/adventure/decode/costume.mjs <juego> [--costume N] [--out dir]');
	process.exit(2);
}
if (isMain) {
const ws = fs.existsSync(path.join(REPO, 'out', 'adventure', target)) ? path.join(REPO, 'out', 'adventure', target) : path.resolve(target);
const GAME = path.basename(ws);
const game = JSON.parse(fs.readFileSync(path.join(ws, 'game.json'), 'utf8'));
const source = path.resolve(ws, game.source);
const gameDir = path.dirname(source);
const roomsJson = JSON.parse(fs.readFileSync(path.join(ws, 'raw', 'index', 'rooms.json'), 'utf8'));
const sim = JSON.parse(fs.readFileSync(path.join(ws, 'decoded', 'sim.json'), 'utf8'));
const roomOf = new Map(roomsJson.rooms.map((r) => [r.room, r]));
const simRoomOf = new Map(sim.rooms.map((r) => [r.room, r]));
const encKey = roomsJson.game?.source?.enc && roomsJson.game.source.enc <= 0xff ? roomsJson.game.source.enc : null;

// Tabla DCOS del índice (mismo formato que DSOU: u16LE nº + sala u8 + offset u32LE).
const idx = fs.readFileSync(source);
if (encKey) for (let i = 0; i < idx.length; i++) idx[i] ^= encKey;
let dcos = null;
{
	let o = 0;
	while (o + 8 <= idx.length) {
		const tag = idx.toString('latin1', o, o + 4);
		const size = idx.readUInt32BE(o + 4);
		if (tag === 'DCOS') { dcos = { off: o + 8 }; break; }
		if (size < 8 || o + size > idx.length) break;
		o += size;
	}
}
if (!dcos) {
	console.error('[costume] el índice no tiene tabla DCOS');
	process.exit(1);
}
const num = idx.readUInt16LE(dcos.off);
const outfits = [];
for (let i = 0; i < num; i++) outfits.push({ id: i + 1, room: idx[dcos.off + 2 + i], off: idx.readUInt32LE(dcos.off + 2 + num + i * 4) });

const outRoot = args.out ? path.resolve(String(args.out)) : path.join(ws, 'decoded', 'png', 'costumes');
fs.mkdirSync(outRoot, { recursive: true });
const only = args.costume ? Number(args.costume) : null;
const manifest = { format: 'adv-costumes/1', game: GAME, costumes: [] };
let ok = 0;
let fail = 0;
for (const c of outfits) {
	if (only && c.id !== only) continue;
	if (!c.room) continue; // id sin costume (igual que los sonidos sin sala)
	const roomEntry = roomOf.get(c.room);
	if (!roomEntry) { fail++; continue; }
	let buf = fs.readFileSync(path.join(gameDir, roomEntry.file));
	const abs = roomEntry.off + c.off;
	if (buf.toString('latin1', abs, abs + 4) !== 'COST' && encKey) {
		buf = Buffer.from(buf);
		for (let i = 0; i < buf.length; i++) buf[i] ^= encKey;
	}
	if (buf.toString('latin1', abs, abs + 4) !== 'COST') { fail++; continue; }
	const size = buf.readUInt32BE(abs + 4);
	const payload = buf.subarray(abs + 8, abs + size);
	let costume;
	try {
		costume = parseCostume(payload);
	} catch (err) {
		manifest.costumes.push({ id: c.id, room: c.room, error: err.message });
		fail++;
		continue;
	}
	const roomPal = simRoomOf.get(c.room)?.palette?.colors ?? [];
	const file = `costume_${String(c.id).padStart(3, '0')}.png`;
	// Hoja única si cabe en un presupuesto razonable (PNG+deflate de ~8M píxeles); si no, una
	// hoja por limb (los walk tienen muchos frames y la rejilla completa sería desmesurada).
	const sheet = renderSheet(costume);
	const files = [];
	if (sheet.W * sheet.H <= 8_000_000) {
		fs.writeFileSync(path.join(outRoot, file), encodeIndexedPng(sheet.W, sheet.H, sheet.pix, roomPal, { transparentIndex: 0 }));
		files.push({ file, w: sheet.W, h: sheet.H });
	} else {
		for (const l of costume.limbs) {
			const sub = { ...costume, limbs: [l], cels: costume.cels.filter((cel) => cel.limb === l.limb) };
			const s = renderSheet(sub);
			const lf = `costume_${String(c.id).padStart(3, '0')}_limb${String(l.limb).padStart(2, '0')}.png`;
			fs.writeFileSync(path.join(outRoot, lf), encodeIndexedPng(s.W, s.H, s.pix, roomPal, { transparentIndex: 0 }));
			files.push({ file: lf, w: s.W, h: s.H, limb: l.limb });
		}
	}
	manifest.costumes.push({
		id: c.id, room: c.room, format: `0x${costume.format.toString(16)}`, mirror: costume.mirror,
		numColors: costume.numColors, palette: costume.palette,
		limbs: costume.limbs,
		cels: costume.cels.map((cel) => ({ limb: cel.limb, frame: cel.frame, w: cel.w, h: cel.h, relX: cel.relX, relY: cel.relY, moveX: cel.moveX, moveY: cel.moveY })),
		sheet: { files, cellW: sheet.cellW, cellH: sheet.cellH },
	});
	ok++;
}
fs.writeFileSync(path.join(outRoot, 'costumes.json'), JSON.stringify(manifest, null, 2) + '\n');
writeDirReadme(outRoot, {
	title: `Costumes (\`COST\`) de ${GAME}`,
	what: 'Hojas PNG (una por costume) con los cels del original por limb/frame, decodificados con el codec de costume V5 (RLE column-major) y coloreados con la paleta de la sala del costume; `costumes.json` guarda el inventario (paleta del costume, limbs y metadatos de cada cel: tamaño y desplazamientos `relX/relY/moveX/moveY`).',
	producedBy: '`node tools/adventure/decode/costume.mjs <juego>` (formato: `costume.cpp:loadCostume`/`base-costume.cpp:byleRLEDecode`).',
	consumedBy: 'cocina de actores (I2), revisión visual y validación de visión.',
	items: [
		{ name: 'costume_<id>.png', desc: 'rejilla de cels (filas = limb, columnas = frame, fondo transparente); si la rejilla es enorme, `costume_<id>_limb<NN>.png` por limb.' },
		{ name: 'costumes.json', desc: '`adv-costumes/1`: inventario por costume y metadatos de cada cel.' },
	],
	related: [{ name: '`../../sim.json`', desc: 'paleta de sala usada para colorear (índices de la paleta del costume).' }],
	notes: ['El color 0 del cel es transparente; los bytes de paleta del costume son índices de la paleta de sala (costume.cpp:setPalette con `_palette[i]=0xFF` por defecto en V5).'],
});
console.log(`[costume] ${GAME}: ${ok} costumes exportados, ${fail} omitidos -> ${outRoot}`);
}
