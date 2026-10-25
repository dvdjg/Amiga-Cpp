#!/usr/bin/env node
// Genera los blobs de assets de `games/500_caverna` desde el arte procedural de
// la vertical slice. Es la fuente host del arte: la demo ya no calcula píxeles en
// runtime, carga estos blobs por manifiesto (`assets.manifest.json`).
//
// Uso: node tools/assets/gen-caverna.mjs
// Salida: out/assets/caverna/*.bin (gitignored; la regenera `src/prebuild.sh`).

import * as fs from 'node:fs';
import * as path from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const OUT = path.join(ROOT, 'out/assets/caverna');
fs.mkdirSync(OUT, { recursive: true });

const kTile = 16;
const kMapWidth = 256;
const kMapHeight = 20;
const kPlanes = 3;
const kFloorRow = 10;

const palette = [
	0x000, 0x122, 0x233, 0x345, 0x468, 0x58b, 0x9cf, 0xfff,
	0x000, 0x141, 0x2b2, 0x421, 0x632, 0x853, 0x0dd, 0xfff,
	0x000, 0xf80, 0xf33, 0xfff, 0xf40, 0xf66, 0xfff, 0xaaa,
	0x421, 0x742, 0x085, 0x0aa, 0x4dd, 0xf6c, 0xf3a, 0x221,
];

function cavePixel(tile, x, y) {
	const bx = (x + (((y >> 2) & 1) * 8)) & 15;
	let wall = ((y & 3) === 0 || bx === 0) ? 2 : 1;
	if (((x * 5 + y * 3) & 15) === 0) wall = 3;
	const glyph = tile & 15;
	if (glyph === 0) return wall;
	if (glyph === 1) return ((x + y * 2) % 13 === 0) ? 0 : wall;
	if (glyph === 2) {
		if (x >= 5 && x <= 10) {
			if (y < 2 || y > 13) return 7;
			return (x & 1) === 0 ? 5 : 3;
		}
		return wall;
	}
	if (glyph === 3) {
		if (y >= 5 && y <= 12) {
			const half = 3 - ((y - 5) >> 1);
			if (x >= 7 - half && x <= 8 + half) {
				if (y <= 6 && (x === 7 || x === 8)) return 7;
				return ((x + y) & 1) === 0 ? 5 : 6;
			}
		}
		return wall;
	}
	if (glyph === 4) {
		const half = 7 - (y >> 1);
		if (y < 10 && x >= 7 - half && x <= 8 + half) {
			return y < 2 ? 2 : (y < 8 ? 3 : 5);
		}
		return wall;
	}
	if (glyph === 5) {
		if (y >= 6 && y <= 8) return y === 6 ? 6 : 4;
		return wall;
	}
	return wall;
}

function terrainPixel(tile, x, y) {
	const glyph = tile & 15;
	if (glyph === 0) return 0;
	if (glyph === 1) {
		if (y >= 9 && y <= 14 && (x === 7 || x === 8)) return 2;
		if (y >= 5 && y <= 8 && x >= 5 && x <= 10) return (y === 5 || x === 8) ? 7 : 6;
		return 0;
	}
	if (glyph === 2 || glyph === 3) {
		if (y <= 1) return ((x + glyph) & 3) === 0 ? 2 : 1;
		if (y === 2) return 1;
		let dirt = ((x + y + glyph) & 7) === 0 ? 4 : 3;
		if (((x * 3 + y) & 15) === 0) dirt = 5;
		return dirt;
	}
	if (glyph >= 4 && glyph <= 7) {
		let dirt = ((x * 3 + y + glyph) & 7) === 0 ? 5 : 3;
		if ((y & 3) === 0) dirt = 4;
		const boulder = (x + glyph * 3) & 7;
		if (y >= 4 && y <= 7 && boulder >= 2 && boulder <= 4) dirt = 5;
		if (y >= 9 && y <= 11 && boulder === 6) dirt = 4;
		return dirt;
	}
	if (glyph >= 8 && glyph <= 11) {
		let rock = ((x + y * 3 + glyph) & 7) === 0 ? 5 : 4;
		if ((x === 7 || x === 8) && (y === 6 || y === 7)) rock = 6;
		if ((x === 7 || x === 8) && (y === 8 || y === 9)) rock = 2;
		if (((x * 5 + glyph) & 15) === 0) rock = 3;
		return rock;
	}
	if (glyph === 12 || glyph === 13) {
		if (y <= 1) return (x & 3) === 0 ? 2 : 1;
		if (y <= 4) return 5;
		return ((x + y + glyph) & 7) === 0 ? 4 : 3;
	}
	return 0;
}

const platforms = [
	[20, 26, 8], [28, 34, 6], [36, 42, 4],
	[58, 64, 8], [66, 72, 6],
	[88, 94, 7], [96, 102, 5],
	[120, 126, 8], [128, 134, 6], [136, 142, 4],
	[164, 170, 7], [172, 178, 5],
	[198, 204, 8], [206, 212, 6],
];

function platformTile(x, y) {
	for (const [x0, x1, py] of platforms) {
		if (y === py && x >= x0 && x <= x1) return true;
	}
	return false;
}

function buildMaps() {
	const cells = new Uint16Array(kMapWidth * kMapHeight);
	const bg = new Uint16Array(kMapWidth * kMapHeight);
	for (let y = 0; y < kMapHeight; ++y) {
		for (let x = 0; x < kMapWidth; ++x) {
			const seg = Math.trunc(x / 16) & 7;
			let top = kFloorRow;
			if (seg === 2 || seg === 6) top = kFloorRow - 1;
			else if (seg === 3 || seg === 7) top = kFloorRow - 2;
			let tile = 0;
			if (y >= top) {
				if (y === top) {
					tile = ((seg === 3 || seg === 7 ? 12 : 2) + (Math.trunc(x / 4) & 1)) & 0xffff;
				} else if (y < 13) {
					tile = (4 + ((Math.trunc(x / 3) + y) & 3)) & 0xffff;
				} else if (y === 13) {
					tile = (8 + ((Math.trunc(x / 2) + y * 3) & 3)) & 0xffff;
				}
			}
			if (tile === 0 && platformTile(x, y)) {
				tile = (12 + (Math.trunc(x / 3) & 1)) & 0xffff;
			}
			cells[y * kMapWidth + x] = tile;

			let b = 0;
			const col = x % 32;
			if (y === 0 && (x & 7) === 3) b = 4;
			else if (col < 2 && y >= 1 && y <= 11) b = 2;
			else if (col >= 2 && col <= 3 && y >= 6 && y <= 10) b = 3;
			else if (((x * 7 + y * 11) % 13) === 0) b = 1;
			bg[y * kMapWidth + x] = b;
		}
	}
	return { cells, bg };
}

function buildRows() {
	const cave = new Uint16Array(16 * 4 * kPlanes * kTile);
	const terrain = new Uint16Array(16 * 4 * kPlanes * kTile);
	for (let glyph = 0; glyph < 16; ++glyph) {
		for (let variant = 0; variant < 4; ++variant) {
			for (let row = 0; row < kTile; ++row) {
				for (let plane = 0; plane < kPlanes; ++plane) {
					let caveWord = 0;
					let terrainWord = 0;
					for (let x = 0; x < kTile; ++x) {
						const bit = 0x8000 >> x;
						if ((cavePixel(glyph, x, row) & (1 << plane)) !== 0) caveWord |= bit;
						if ((terrainPixel(glyph, x, row) & (1 << plane)) !== 0) terrainWord |= bit;
					}
					const idx = ((glyph * 4 + variant) * kPlanes + plane) * kTile + row;
					cave[idx] = caveWord & 0xffff;
					terrain[idx] = terrainWord & 0xffff;
				}
			}
		}
	}
	return { cave, terrain };
}

function actorPixel(type, phase, x, y) {
	if (type === 0) {
		if (y >= 1 && y <= 6 && x >= 4 && x <= 11) {
			if (x === 4 || x === 11 || y === 1 || y === 6) return 2;
			if (y >= 3 && y <= 4 && x >= 6 && x <= 9) return 3;
			return 1;
		}
		if (y >= 7 && y <= 12 && x >= 4 && x <= 11) return (x === 4 || x === 11) ? 2 : 1;
		if (y >= 8 && y <= 10 && (x === 2 || x === 3 || x === 12 || x === 13)) return 1;
		if (y >= 13 && y <= 15 && ((x >= 4 && x <= 6) || (x >= 9 && x <= 11))) {
			return (y === 15 || x === 4 || x === 11) ? 2 : 1;
		}
		return 0;
	}
	if (type === 1) {
		if (y >= 4 && y <= 9 && x >= 6 && x <= 9) {
			if (y === 5 && (x === 6 || x === 9)) return 3;
			return (x === 6 || x === 9 || y === 9) ? 2 : 1;
		}
		const wy = (y + (phase & 1)) & 0xff;
		if (wy >= 3 && wy <= 8) {
			const edge = wy <= 5 ? wy - 2 : 9 - wy;
			const reach = (2 + edge * 2) & 0xff;
			if ((x <= 5 && x >= ((6 - reach) & 0xff)) ||
			    (x >= 10 && x <= ((9 + reach) & 0xff))) return 2;
		}
		return 0;
	}
	if (type === 2) {
		if (y >= 4 && y <= 14) {
			let half = 2 + ((y - 4) >> 1);
			if (half > 7) half = 7;
			if (x >= 8 - half && x < 8 + half) {
				if (y === 4 || x === 8 - half || x === 7 + half) return 2;
				if (y >= 7 && y <= 8 && (x === 5 || x === 10)) return 3;
				return 1;
			}
		}
		return 0;
	}
	if (type === 3) {
		if (y >= 5 && y <= 10 && x >= 3 && x <= 12) {
			if (x === 3 || x === 12 || y === 5 || y === 10) return 2;
			if (y === 7 && (x === 5 || x === 10)) return 3;
			return 1;
		}
		if (y >= 6 && y <= 8 && ((x >= 1 && x <= 3) || (x >= 12 && x <= 14))) return 2;
		if (y >= 11 && y <= 14 && ((x + y + phase) & 1) === 0 && (x <= 4 || x >= 11)) return 1;
		return 0;
	}
	return 0;
}

function buildActorSheet(type, phase) {
	const words = new Uint16Array(32);
	for (let y = 0; y < 16; ++y) {
		let dat = 0;
		let datb = 0;
		for (let x = 0; x < 16; ++x) {
			const bit = 0x8000 >> x;
			const color = actorPixel(type, phase, x, y);
			if ((color & 1) !== 0) dat |= bit;
			if ((color & 2) !== 0) datb |= bit;
		}
		words[y * 2] = dat & 0xffff;
		words[y * 2 + 1] = datb & 0xffff;
	}
	return words;
}

function writeU16(file, words) {
	const b = Buffer.alloc(words.length * 2);
	for (let i = 0; i < words.length; ++i) b.writeUInt16BE(words[i], i * 2);
	fs.writeFileSync(path.join(OUT, file), b);
}

function writeBytes(file, bytes) {
	fs.writeFileSync(path.join(OUT, file), Buffer.from(bytes));
}

function genSfx() {
	const rate = 11025;
	const make = (n, fn) => {
		const b = Buffer.alloc(n);
		for (let i = 0; i < n; ++i) {
			const v = Math.max(-127, Math.min(127, Math.round(fn(i / rate, i, n) * 110)));
			b[i] = v & 0xff;
		}
		return b;
	};
	writeBytes('sfx_jump.raw', make(256, (t, i, n) => {
		const f = 220 + 880 * (i / n);
		return Math.sin(2 * Math.PI * f * t) * (1 - i / n);
	}));
	writeBytes('sfx_stomp.raw', make(256, (t, i, n) => {
		const f = 90 + 40 * (1 - i / n);
		return (Math.sin(2 * Math.PI * f * t) + (Math.random() * 2 - 1) * 0.4) * (1 - i / n);
	}));
	writeBytes('sfx_damage.raw', make(384, (t, i, n) => {
		const f = 440 - 300 * (i / n);
		return Math.sin(2 * Math.PI * f * t) * (1 - i / n);
	}));
	writeBytes('sfx_land.raw', make(128, (t, i, n) => {
		const f = 130;
		return Math.sin(2 * Math.PI * f * t) * (1 - i / n) * 0.6;
	}));
}

const { cells, bg } = buildMaps();
const { cave, terrain } = buildRows();
writeU16('palette.bin', Uint16Array.from(palette));
writeU16('map_pf2.bin', cells);
writeU16('map_pf1.bin', bg);
writeU16('cave_rows.bin', cave);
writeU16('terrain_rows.bin', terrain);
writeU16('player.bin', Uint16Array.from([...buildActorSheet(0, 0), ...buildActorSheet(0, 1)]));
writeU16('bat.bin', Uint16Array.from([...buildActorSheet(1, 0), ...buildActorSheet(1, 1)]));
writeU16('slime.bin', Uint16Array.from([...buildActorSheet(2, 0), ...buildActorSheet(2, 1)]));
writeU16('crab.bin', Uint16Array.from([...buildActorSheet(3, 0), ...buildActorSheet(3, 1)]));
genSfx();
console.log(`[gen-caverna] blobs escritos en ${path.relative(ROOT, OUT)}`);
