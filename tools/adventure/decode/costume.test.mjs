// Tests del decodificador de costumes (`decode/costume.mjs`): cabecera v5 sintética y
// decodificación RLE column-major (port de `ByleRLEDecode_Mode0`).
// Uso: node --test tools/adventure/decode/costume.test.mjs

import test from 'node:test';
import assert from 'node:assert/strict';
import { decodeCel, parseCostume } from './costume.mjs';

// Layout sintético (índices de `base`, con `base = payload - 6`):
//   [6] numAnim = 1 · [7] formato 0x58 · [8..24) paleta · [24] animCmds = 160
//   [26..58) frameOffs (16 u16): limb 0 → tabla en 96 · [58..90) dataOffs = 0
//   [96..160) tabla del limb 0 (entrada 0 = cel en 164) · [164..180) cel 2x2 + RLE
function makeCostumeBase() {
	const base = Buffer.alloc(200);
	base[6] = 1;
	base[7] = 0x58;
	for (let i = 0; i < 16; i++) base[8 + i] = i + 1;
	base.writeUInt16LE(160, 24);
	base.writeUInt16LE(96, 26);
	base.writeUInt16LE(164, 96);
	base.writeUInt16LE(2, 164);
	base.writeUInt16LE(2, 166);
	base.writeInt16LE(-1, 168);
	base.writeInt16LE(-2, 170);
	base.writeInt16LE(0, 172);
	base.writeInt16LE(1, 174);
	base[176] = 0x52; // columna 0: color 5, len 2
	base[177] = 0x72; // columna 1: color 7, len 2
	return base;
}

test('costume: cabecera v5, tabla por limb y RLE column-major', () => {
	const base = makeCostumeBase();
	const costume = parseCostume(base.subarray(6)); // payload = bytes tras `COST`+tamaño
	assert.equal(costume.format, 0x58);
	assert.equal(costume.mirror, false);
	assert.equal(costume.numColors, 16);
	assert.deepEqual(costume.palette.slice(0, 4), [1, 2, 3, 4]);
	assert.deepEqual(costume.limbs, [{ limb: 0, frames: 1 }]);
	assert.equal(costume.cels.length, 1);
	const cel = costume.cels[0];
	assert.deepEqual({ w: cel.w, h: cel.h, relX: cel.relX, relY: cel.relY, moveX: cel.moveX, moveY: cel.moveY },
		{ w: 2, h: 2, relX: -1, relY: -2, moveX: 0, moveY: 1 });
	// Columna 0 = color 5, columna 1 = color 7 → por filas: [5,7][5,7]
	assert.deepEqual([...cel.pix], [5, 7, 5, 7]);
});

test('costume: un run que cruza el fin de columna continúa en la siguiente', () => {
	const base = makeCostumeBase();
	base[176] = 0x69; // run de 9 píxeles de color 6 (> 2 columnas × 2 filas)
	const costume = parseCostume(base.subarray(6));
	const cel = costume.cels[0];
	assert.deepEqual([...cel.pix], [6, 6, 6, 6]);
});

test('costume: stream truncado lanza (no cuelga)', () => {
	const base = Buffer.alloc(24);
	base.writeUInt16LE(4, 0);
	base.writeUInt16LE(4, 2);
	assert.throws(() => decodeCel(base, 0), /truncado/);
});

test('costume OLD256: resuelve códigos del animCmds y excluye comandos especiales 0x79+', () => {
	const base = makeCostumeBase();
	base[160] = 0x79;
	base.writeUInt16LE(180, 58);
	base.writeUInt16LE(0x8000, 180);
	base[182] = 0;
	base[183] = 0;
	const costume = parseCostume(base.subarray(6), { useAnimationCommands: true });
	assert.equal(costume.cels.length, 0);
});

test('costume OLD256: el código animado apunta al índice de cel correspondiente', () => {
	const base = makeCostumeBase();
	base[160] = 0;
	base.writeUInt16LE(180, 58);
	base.writeUInt16LE(0x8000, 180);
	base[182] = 0;
	base[183] = 0;
	const costume = parseCostume(base.subarray(6), { useAnimationCommands: true });
	assert.equal(costume.cels.length, 1);
	assert.equal(costume.cels[0].frameCode, 0);
	assert.deepEqual([...costume.cels[0].pix], [5, 7, 5, 7]);
});
