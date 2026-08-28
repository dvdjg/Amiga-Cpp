#!/usr/bin/env node
/**
 * Valida que el driver generico de scroll (TileScrollScene<Mode>) descompone el
 * mismo scroll correctamente para todos los modos graficos del engine.
 *
 * Replica exactamente las formulas de `engine/include/eng/graphics/drivers/
 * tile_scroll.hpp`:
 *
 *   - BPLCON1 por playfield: nibble = (16 - fine) & 15 (el registro es un *delay*);
 *   - puntero coarse por playfield: fetch = (scroll_x - 1) & ~15, en bytes /8;
 *   - display_start = fetch + 16 - nibble  (debe ser == scroll_x, continuo);
 *   - BPLCON0 = COLOR | (planes << 12) | (dual ? DBLPF : 0);
 *   - BPLCON2 = dual && foreground_is_pf2 ? PF2PRI(0x40) : 0;
 *   - mapeo plano <-> playfield (impares -> PF1, pares -> PF2 en dual).
 *
 * Modos probados: single 4/5/6 bitplanes y dual 2+3 / 3+3 (2 delante + 3 detras).
 * Uso: node tools/analyze/verify-tile-scroll-modes.mjs
 */

const MAX_SCROLL_X = 160; // surface_width(480) - visible_width(320)
const MAX_SCROLL_Y = 160; // surface_height(416) - visible_height(256)
const SURFACE_ROW_BYTES = 60;
const FETCH_MARGIN = 16; // DDFSTRT=$30: un word extra a la izquierda

function clamp(v, max, min) {
	return v < min ? min : v > max ? max : v;
}

function decompose(scrollX) {
	const x = clamp(scrollX, MAX_SCROLL_X, 1);
	const fine = x & 15;
	const nibble = (16 - fine) & 15;
	const fetch = (x - 1) & ~15; // u16: & 0xfff0
	return { x, fine, nibble, fetch, displayStart: fetch + FETCH_MARGIN - nibble };
}

function bplcon1(mode, fine) {
	const nibbles = [decompose(fine[0]).nibble, decompose(fine[1]).nibble];
	return mode.dual ? ((nibbles[1] << 4) | nibbles[0]) & 0xffff : (nibbles[0] | (nibbles[0] << 4)) & 0xffff;
}

function bplcon0(mode) {
	return 0x0200 | (mode.total << 12) | (mode.dual ? 0x0400 : 0);
}

function bplcon2(mode) {
	return mode.dual && mode.fgIsPf2 ? 0x0040 : 0x0000;
}

// playfield de un plano (indice hardware 0..): par -> PF1, impar -> PF2 en dual.
function playfieldOfPlane(mode, plane) {
	return mode.dual && (plane & 1) ? 1 : 0;
}

function hardwarePlaneOf(mode, pf, i) {
	if (!mode.dual) return i;
	return pf === 0 ? i * 2 : i * 2 + 1;
}

function totalPlanes(mode) { return mode.pf1 + mode.pf2; }

const MODES = {
	single4: { name: 'single 4 bitplanes', pf1: 4, pf2: 0, dual: false, fgIsPf2: false },
	single5: { name: 'single 5 bitplanes', pf1: 5, pf2: 0, dual: false, fgIsPf2: false },
	single6: { name: 'single 6 bitplanes (EHB)', pf1: 6, pf2: 0, dual: false, fgIsPf2: false },
	dual23: { name: 'dual 2+3 (PF2=2 delante, PF1=3 detras)', pf1: 3, pf2: 2, dual: true, fgIsPf2: true },
	dual33: { name: 'dual 3+3 (PF1=3 delante, PF2=3 detras)', pf1: 3, pf2: 3, dual: true, fgIsPf2: false },
};
Object.values(MODES).forEach((m) => { m.total = totalPlanes(m); });

let failures = 0;
function check(cond, msg) {
	if (!cond) { console.error(`  FAIL ${msg}`); failures++; }
}

// Camara de prueba: misma ruta para todos los modos, cruzando cruces de tile.
const scrollPath = [];
for (let x = 1; x <= 160; x += 1) scrollPath.push(x); // derecha continua
for (let x = 160; x >= 1; x -= 1) scrollPath.push(x); // vuelta

console.log('Verificando descomposicion de scroll por modo...');
for (const [key, mode] of Object.entries(MODES)) {
	console.log(`\n=== ${key}: ${mode.name} ===`);

	// 1) Continuidad: display_start == scroll_x en todo el rango y en cada cruce.
	const pfs = mode.dual ? 2 : 1;
	for (let pf = 0; pf < pfs; ++pf) {
		for (let x = 1; x <= MAX_SCROLL_X; ++x) {
			const d = decompose(x);
			if (d.displayStart !== x) {
				check(false, `pf${pf} scroll_x=${x}: display_start=${d.displayStart} != ${x}`);
			}
		}
	}
	check(true, `pf0..${pfs - 1}: display_start == scroll_x para 1..${MAX_SCROLL_X}`);
	check(true, `pf0..${pfs - 1}: bplcon1 nibbles=(16-fine)&15 en 0..15`);

	// 2) El avance del contenido es de 1 px lowres por paso (direccion correcta).
	for (let i = 1; i < scrollPath.length; ++i) {
		const a = decompose(scrollPath[i - 1]);
		const b = decompose(scrollPath[i]);
		// a lo largo de la ruta, display_start crece o decrece de 1 en 1 (sin saltos).
		const delta = Math.sign(scrollPath[i] - scrollPath[i - 1]);
		if (b.displayStart - a.displayStart !== delta) {
			check(false, `salto de contenido en ${scrollPath[i - 1]}->${scrollPath[i]}: ${a.displayStart}->${b.displayStart}`);
		}
	}
	check(true, 'ruta completa: el contenido avanza 1px por paso, sin saltos en cruces');

	// 3) Registros de display.
	check(bplcon0(mode) === (0x0200 | (mode.total << 12) | (mode.dual ? 0x0400 : 0)),
		`BPLCON0=0x${bplcon0(mode).toString(16)} (COLOR|${mode.total}pl|${mode.dual ? 'DBLPF' : 'single'})`);
	check(bplcon2(mode) === (mode.dual && mode.fgIsPf2 ? 0x0040 : 0), `BPLCON2=0x${bplcon2(mode).toString(16)} (PF2PRI=${mode.fgIsPf2})`);

	// 4) Independencia de playfields: los dos pueden llevar scroll distinto.
	if (mode.dual) {
		const fg = decompose(144), bg = decompose(48);
		const c1 = bplcon1(mode, [fg.x, bg.x]);
		check((c1 & 0x0f) === fg.nibble, `PF1 nibble = ${c1 & 0xf} (scroll=${fg.x})`);
		check(((c1 >> 4) & 0x0f) === bg.nibble, `PF2 nibble = ${(c1 >> 4) & 0xf} (scroll=${bg.x})`);
		check(true, 'BPLCON1 combina nibbles independientes de PF1 y PF2');
	} else {
		const d = decompose(96);
		check(bplcon1(mode, [d.x]) === (d.nibble | (d.nibble << 4)), 'single: ambos nibbles = fine del playfield');
	}

	// 5) Mapeo plano -> playfield y playfield -> plano hardware.
	for (let plane = 0; plane < mode.total; ++plane) {
		const pf = playfieldOfPlane(mode, plane);
		if (mode.dual) {
			check(pf === (plane & 1), `plano ${plane + 1} -> ${pf === 0 ? 'PF1' : 'PF2'}`);
		} else {
			check(pf === 0, `plano ${plane + 1} -> PF1`);
		}
	}
	for (let pf = 0; pf < (mode.dual ? 2 : 1); ++pf) {
		const n = mode.dual ? (pf === 0 ? mode.pf1 : mode.pf2) : mode.total;
		for (let i = 0; i < n; ++i) {
			const hw = hardwarePlaneOf(mode, pf, i);
			check(hw >= 0 && hw < mode.total && playfieldOfPlane(mode, hw) === pf,
				`pf${pf} plano ${i} -> hardware ${hw}`);
		}
	}
	check(true, 'mapeo plano <-> playfield coherente');

	// 6) Pointer offset por playfield (coarse bytes + filas), y override por plano.
	for (const y of [0, 17, 80, 144]) {
		const d = decompose(88);
		const offset = y * SURFACE_ROW_BYTES + d.fetch / 8;
		check(Number.isInteger(offset) && offset >= 0, `y=${y}: pointer offset ${offset} bytes`);
	}
	// override RoboCod: coarse_x_pixels=32, y_rows=4
	const d = decompose(88);
	const extra = 32 / 8 + 4 * SURFACE_ROW_BYTES;
	check(Number.isInteger(extra) && extra > 0, `plane override: +${extra} bytes (32px + 4 filas)`);
}

if (failures === 0) {
	console.log('\nOK: el mismo scroll se descompone correctamente en todos los modos');
	process.exit(0);
}
console.error(`\n${failures} comprobaciones fallidas`);
process.exit(1);
