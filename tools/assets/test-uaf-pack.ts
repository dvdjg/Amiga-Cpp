#!/usr/bin/env node
/**
 * Test host de `tools/assets/uaf-pack.ts` (formato UAF-R + chunky→planar).
 */
import {
	packUaf, parseUaf, bitplanesFromIndexed, paletteChunkData,
	bitplanesChunkData, stringsChunkData, tilesChunkData, UafChunkType, UAF_MAGIC,
} from './uaf-pack.js';

let failures = 0;
function check(ok: boolean, msg: string) {
	if (!ok) {
		console.error(`  [FAIL] ${msg}`);
		failures++;
	}
}

function testRoundtrip() {
	console.log('uaf-pack: contenedor round-trip');
	const pal = paletteChunkData([0x0abc, 0x0123]);
	const blob = packUaf([
		{ type: UafChunkType.Palette, count: 2, data: pal },
		{ type: UafChunkType.Samples, count: 3, data: Uint8Array.from([1, 2, 3]) },
	]);
	check(blob.readUInt32BE(0) === UAF_MAGIC, 'magic UAFR');
	check(blob.readUInt16BE(6) === 2, 'chunk_count = 2');

	const chunks = parseUaf(blob);
	check(chunks.length === 2, '2 chunks parseados');
	check(chunks[0].type === UafChunkType.Palette && chunks[0].count === 2 && chunks[0].size === 4,
		'chunk 0 paleta');
	check(blob[chunks[0].offset] === 0x0a && blob[chunks[0].offset + 1] === 0xbc, 'paleta big-endian');
	check(chunks[1].type === UafChunkType.Samples && chunks[1].size === 3, 'chunk 1 sample');
	// 8 (header) + pad4(8+4)=12 + pad4(8+3)=12 = 32
	check(blob.length === 32, `longitud con padding (${blob.length})`);
}

function testChunkyToPlanar() {
	console.log('uaf-pack: chunky indexado -> bitplanes');
	const p1 = bitplanesFromIndexed(8, 1, 1, Uint8Array.from([1, 0, 0, 0, 0, 0, 0, 0]));
	check(p1.length === 1 && p1[0] === 0x80, '1 plano: píxel 0 -> 0x80');

	const p2 = bitplanesFromIndexed(8, 1, 2, Uint8Array.from([0, 1, 2, 3, 0, 0, 0, 0]));
	check(p2[0] === 0x50, `plano 0 (${p2[0].toString(16)})`); // píxeles 1 y 3
	check(p2[1] === 0x30, `plano 1 (${p2[1].toString(16)})`); // píxeles 2 y 3

	const geom = bitplanesChunkData(8, 1, 2, 0, p2);
	check(geom[0] === 0 && geom[1] === 8, 'cabecera width');
	check(geom[4] === 0 && geom[5] === 1, 'cabecera row_bytes');
	check(geom[6] === 2, 'cabecera planes');
	check(geom[10] === 0x50 && geom[11] === 0x30, 'planos tras cabecera');
}

function testErrors() {
	console.log('uaf-pack: errores de validación');
	let threw = false;
	try { parseUaf(Buffer.alloc(8)); } catch { threw = true; }
	check(threw, 'magic inválido lanza');
	threw = false;
	try { parseUaf(Buffer.alloc(4)); } catch { threw = true; }
	check(threw, 'blob corto lanza');
	threw = false;
	try { bitplanesFromIndexed(10, 1, 1, new Uint8Array(10)); } catch { threw = true; }
	check(threw, 'width no múltiplo de 8 lanza');
}

function testConsumers() {
	console.log('uaf-pack: consumidores strings/tiles');
	const s = stringsChunkData(['hola', 'mundo']);
	check(s.length === 11, `strings total (${s.length})`); // 4+1+5+1
	check(s[4] === 0 && s[10] === 0, 'strings NUL-terminadas');
	check(Buffer.from(s).toString('latin1').split('\0').slice(0, 2).join('|') === 'hola|mundo',
		'strings contenido');

	const t = tilesChunkData([Uint8Array.from([1, 2]), Uint8Array.from([3, 4])]);
	check(t.length === 4 && t[0] === 1 && t[3] === 4, 'tiles concatenados');
	let threw = false;
	try { tilesChunkData([Uint8Array.from([1, 2]), Uint8Array.from([3])]); } catch { threw = true; }
	check(threw, 'tile de tamaño distinto lanza');
}

testRoundtrip();
testChunkyToPlanar();
testConsumers();
testErrors();

if (failures === 0) {
	console.log('OK: uaf-pack validado (contenedor + chunky→planar + strings/tiles + errores).');
	process.exit(0);
}
console.error(`FAIL: ${failures} comprobacion(es) fallaron`);
process.exit(1);
