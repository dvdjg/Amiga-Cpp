#!/usr/bin/env node
// Genera un juego SCUMM-V5 **sintético** mínimo (fixture propio, sin material con derechos)
// para los tests del pipeline: `fixture.000` (índice) + `fixture.001` (datos), ambos XOR 0x69
// como los juegos con GF_USE_KEY. Determinista: mismas entradas -> mismos bytes.
//
// Contenido: 1 sala ("test-room") con RMHD + BOXD, un SCRP y un EXCD mínimos.
// Uso: node tools/adventure/sim/make-fixture.mjs --out <dir>

import fs from 'node:fs';
import path from 'node:path';

const ENC = 0x69;

function block(tag, payload) {
	const head = Buffer.alloc(8);
	head.write(tag, 0, 4, 'latin1');
	head.writeUInt32BE(8 + payload.length, 4);
	return Buffer.concat([head, payload]);
}

function xor(buf, key) {
	const out = Buffer.from(buf);
	for (let i = 0; i < out.length; i++) out[i] ^= key;
	return out;
}

function parseArgs(argv) {
	const out = {};
	for (let i = 0; i < argv.length; i++) if (argv[i] === '--out') out.out = argv[++i];
	return out;
}

const args = parseArgs(process.argv.slice(2));
const dir = args.out ?? path.join(process.cwd(), 'out', 'tmp', 'sim-fixture');
fs.mkdirSync(dir, { recursive: true });

// --- datos (fixture.001) ---------------------------------------------------------------

const rmhd = Buffer.from([0x40, 0x01, 0xc8, 0x00, 0x00, 0x00]); // 320 px / 200 líneas (LE)
// BOXD: 1 caja completa V5 (20 B): ul(0,0) ur(64,0) lr(64,64) ll(0,64) + mask/flags/scale(255).
const boxd = Buffer.from([
	0x01, 0x00, // numBoxes (u16 LE)
	0x00, 0x00, 0x00, 0x00, // ul
	0x40, 0x00, 0x00, 0x00, // ur
	0x40, 0x00, 0x40, 0x00, // lr
	0x00, 0x00, 0x40, 0x00, // ll
	0x0f, 0x00, 0xff, 0x00, // mask, flags, scale (u16 LE, 255 = 100 %)
]);
const boxm = Buffer.from([0x00, 0xff]); // adyacencia de la caja 0: [0]
const clut = Buffer.from(Array.from({ length: 768 }, (_, i) => (i % 3 === 0 ? i / 3 : i % 3 === 1 ? (i * 2) & 0xff : 255 - (i / 3) | 0)));
// Objeto 1: CDHD v5 (obj_id=1, x=5,y=6 → ×8, w=h=2 → 16 px, walk 40,48, dir 2) + VERB mínimo.
const cdhd = Buffer.from([0x01, 0x00, 0x05, 0x06, 0x02, 0x02, 0x00, 0x00, 0x28, 0x00, 0x30, 0x00, 0x02]);
const verb = Buffer.from([0x80, 0xa0]);
const obcd = block('OBCD', Buffer.concat([block('CDHD', cdhd), block('VERB', verb)]));
// Imagen del objeto 1: IMHD de V5 PC (16 B): obj_id=1, image_count=1 (sin lista de hotspots).
const imhd = Buffer.alloc(16);
imhd.writeUInt16LE(1, 0);
imhd.writeUInt16LE(1, 2);
const obim = block('OBIM', block('IMHD', imhd));
const scrpOps = Buffer.from([0x80, 0xa0]); // breakHere(); stopObjectCode();
const excdOps = Buffer.from([0x80, 0xa0]);

const room = block('ROOM', Buffer.concat([block('RMHD', rmhd), block('BOXD', boxd), block('BOXM', boxm), block('CLUT', clut), obcd, obim]));
const scrp = block('SCRP', scrpOps);
const excd = block('EXCD', excdOps);
const lflf = block('LFLF', Buffer.concat([room, scrp, excd]));

// LOFF: count(1) + sala(1) + offset(u32 LE) del bloque ROOM dentro del fichero.
const loffSize = 8 + 1 + 1 + 4;
const roomOff = 8 + loffSize + 8; // LECF(8) + LOFF + cabecera LFLF(8)
const loff = block('LOFF', Buffer.from([0x01, 0x01, roomOff & 0xff, (roomOff >> 8) & 0xff, (roomOff >> 16) & 0xff, (roomOff >> 24) & 0xff]));
const lecf = block('LECF', Buffer.concat([loff, lflf]));

// --- índice (fixture.000) ---------------------------------------------------------------

const name = 'test-room';
const rnamPayload = Buffer.concat([
	Buffer.from([0x01]),
	Buffer.from([...name.slice(0, 9)].map((c) => c.charCodeAt(0) ^ 0xff)),
	Buffer.from([0x00]),
]);
const maxs = block('MAXS', Buffer.alloc(18, 0x11));
const droo = block('DROO', Buffer.concat([Buffer.from([0x01, 0x00]), Buffer.from([0x01]), Buffer.from([0x00, 0x00, 0x00, 0x00])]));
const dscr = block('DSCR', Buffer.concat([Buffer.from([0x01, 0x00]), Buffer.from([0x01]), Buffer.from([0x00, 0x00, 0x00, 0x00])]));
const index = Buffer.concat([block('RNAM', rnamPayload), maxs, droo, dscr]);

fs.writeFileSync(path.join(dir, 'fixture.000'), xor(index, ENC));
fs.writeFileSync(path.join(dir, 'fixture.001'), xor(lecf, ENC));
console.log(`[make-fixture] ${path.join(dir, 'fixture.000')} + fixture.001 (enc 0x${ENC.toString(16)})`);
