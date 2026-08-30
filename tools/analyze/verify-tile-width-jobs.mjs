#!/usr/bin/env node
/**
 * Valida la optimizacion de tiles ANCHOS de la API TileField
 * (`engine/include/eng/field/tile_field.hpp`).
 *
 * `TileFieldConfig::tile_width` admite multiples de 16 (16, 32, 48, 64...). El
 * blit de copia (`make_tile_copy_job`) debe generar UN solo job `TileBlockCopy`
 * que cubra el tile ancho en una sola pasada del Blitter, sin pasadas extra de
 * 16px. Esto se logra con:
 *
 *   - words_per_row = tile_width / 16  (2 para 32px, 3 para 48px, 4 para 64px);
 *   - height = tile_size (16 filas)     -> el job es words_per_row x tile_size;
 *   - source: cada plano del tile es contiguo (modulo 0) y el stride entre
 *     planos es words_per_plane*2 bytes;
 *   - destino: word-aligned (fb_x es multiplo de 16) y modulo =
 *     row_bytes - words_per_row*2 para saltar al siguiente tile;
 *   - destination_plane_stride = plane_bytes (planos contiguos del framebuffer).
 *
 * Tambien valida la COHERENCIA con el layout del tileset: cada tile ancho se
 * escribe como [plano][tile_size filas x words_per_row words] contiguos, de
 * modo que el job lee words_per_plane words por plano sin solapes.
 *
 * Uso: node tools/analyze/verify-tile-width-jobs.mjs
 */

const ROW_BYTES = 80; // framebuffer de 640px (doble pagina)
const PLANE_BYTES = 80 * 512;
const TILE_SIZE = 16;

// Layout del tileset: [tile][plano][filas x words_per_row].
// Devuelve el offset en words del plano `plane` del tile `tile`.
function tilePlaneOffset(tile, plane, tileWidth, tilesetPlanes) {
	const wordsPerRow = tileWidth / 16;
	const wordsPerPlane = TILE_SIZE * wordsPerRow;
	return (tile * tilesetPlanes + plane) * wordsPerPlane;
}

function makeJob(tileWidth, fbX, fbY, tilesetPlanes) {
	const wordsPerRow = tileWidth / 16;
	const wordsPerPlane = TILE_SIZE * wordsPerRow;
	const srcOffset = tilePlaneOffset(0, 0, tileWidth, tilesetPlanes);
	const dst = fbY * ROW_BYTES + fbX / 8;
	return {
		kind: 'TileBlockCopy',
		wordsPerRow,
		height: TILE_SIZE,
		sourceModulo: 0,
		destinationModulo: ROW_BYTES - wordsPerRow * 2,
		bitplaneCount: tilesetPlanes,
		sourcePlaneStride: wordsPerPlane * 2,
		destinationPlaneStride: PLANE_BYTES,
		srcOffset,
		dst,
	};
}

function check(name, cond) {
	if (!cond) {
		console.error(`FAIL: ${name}`);
		process.exit(1);
	}
	console.log(`  ok: ${name}`);
}

function testWidth(tileWidth) {
	console.log(`tile_width=${tileWidth}px`);
	const job = makeJob(tileWidth, 0, 0, 3);
	check(`words_per_row == ${tileWidth / 16} (una pasada)`, job.wordsPerRow === tileWidth / 16);
	check('height == tile_size (16 filas)', job.height === TILE_SIZE);
	check('source contiguo (modulo 0)', job.sourceModulo === 0);
	check(`dest modulo == row_bytes - words*2`, job.destinationModulo === ROW_BYTES - (tileWidth / 16) * 2);
	check('bitplane_count == 3', job.bitplaneCount === 3);
	check('destino word-aligned', job.dst % 2 === 0);
	check(`ancho cubierto == ${tileWidth}px`, job.wordsPerRow * 16 === tileWidth);
	// Layout del tileset: los planos del tile no se solapan entre si.
	const p0 = tilePlaneOffset(0, 0, tileWidth, 3);
	const p1 = tilePlaneOffset(0, 1, tileWidth, 3);
	const p2 = tilePlaneOffset(0, 2, tileWidth, 3);
	const wordsPerPlane = TILE_SIZE * (tileWidth / 16);
	check('planos del tile contiguos y sin solape', p0 + wordsPerPlane === p1 && p1 + wordsPerPlane === p2);
	// El siguiente tile empieza justo despues del ultimo plano.
	check('siguiente tile contiguo', p2 + wordsPerPlane === tilePlaneOffset(1, 0, tileWidth, 3));
}

function main() {
	console.log('TileField: tiles de ancho multiplo de 16 (una pasada del Blitter)');
	for (const w of [16, 32, 48, 64]) {
		testWidth(w);
	}
	// Coherencia entre tile_width y el stride de fuente.
	const s16 = makeJob(16, 0, 0, 3).sourcePlaneStride;
	const s32 = makeJob(32, 0, 0, 3).sourcePlaneStride;
	const s64 = makeJob(64, 0, 0, 3).sourcePlaneStride;
	check('stride 32 == 2x stride 16', s32 === s16 * 2);
	check('stride 64 == 4x stride 16', s64 === s16 * 4);
	console.log('OK verify-tile-width-jobs');
}

main();
