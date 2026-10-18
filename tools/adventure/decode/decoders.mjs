// Decodificadores **puros** de bloques SCUMM V5 a estructuras semánticas (para el SIM v2).
// Entrada: el *payload* del bloque (sin cabecera tag+size), tal y como lo vuelca el frontend
// en `raw/rooms/room_N/*.bin`. Referencias del oráculo `../SCUMM/scummvm-2026.4.0/engines/scumm/`:
//   - walkboxes: `boxes.cpp` (`SIZEOF_BOX` = 8×s16 + mask + flags + scale; `getBoxMatrixBaseAddr`)
//   - objetos:   `object.cpp:1074` (`cdhd->v5`: obj_id, x, y, w, h, flags, parent, walk_x, walk_y,
//                actordir; coordenadas ×8) e `object.h:138` (`ImageHeader.old` para IMHD)
//   - paleta:    `palette.cpp:1546` (`getPalettePtr`: CLUT = 256×3 B; EPAL = 256 B, par de índices EGA por color)
//   - sala:      `room.cpp:343` (`RMHD.old.width/height` u16 LE)
// Todas las funciones devuelven `null` cuando el bloque no tiene el tamaño mínimo.

/** Tamaño de una caja V5 en el bloque BOXD (8×s16 + mask + flags + scale). */
export const BOX_SIZE_V5 = 20;

/** Cabecera CDHD v5 (`object.h:107`): obj_id u16 + x,y,w,h,flags,parent (6 B) + walk_x/y s16 + actordir. */
export const CDHD_V5_SIZE = 13;
/** Cabecera IMHD de V5 PC: 16 bytes (obj_id u16, image_count u16 + 12 B); las variantes con
 * lista de hotspots (`ImageHeader.old`, 78 B) solo aparecen en otras plataformas. */
export const IMHD_V5_SIZE = 16;
/** Tamaño de `ImageHeader.old` con hotspots (FMTowns y variantes). */
export const IMHD_OLD_SIZE = 78;

/**
 * Decodifica `BOXD` (cajas) y opcionalmente `BOXM` (adyacencia) de una sala.
 * \param boxd payload del bloque BOXD.
 * \param boxm payload del bloque BOXM (o null).
 * \return { numBoxes, boxes, adjacency } o null si BOXD es demasiado corto.
 */
export function decodeWalkboxes(boxd, boxm) {
	if (!boxd || boxd.length < 2) return null;
	const count = boxd.readUInt16LE(0);
	const boxes = [];
	for (let i = 0; i < count && 2 + (i + 1) * BOX_SIZE_V5 <= boxd.length; i++) {
		const o = 2 + i * BOX_SIZE_V5;
		const g = (k) => boxd.readInt16LE(o + k * 2);
		boxes.push({
			id: i,
			ul: [g(0), g(1)],
			ur: [g(2), g(3)],
			lr: [g(4), g(5)],
			ll: [g(6), g(7)],
			mask: boxd[o + 16],
			flags: boxd[o + 17],
			scale: boxd.readUInt16LE(o + 18),
		});
	}
	let adjacency = null;
	if (boxm && boxm.length > 0) {
		adjacency = [];
		let q = boxm[0] === 0xff ? 1 : 0; // prefijo 0xFF opcional (`getBoxMatrixBaseAddr`)
		for (let i = 0; i < count && q < boxm.length; i++) {
			const ids = [];
			while (q < boxm.length && boxm[q] !== 0xff) ids.push(boxm[q++]);
			q++;
			adjacency.push(ids);
		}
	}
	return { numBoxes: count, boxes, adjacency };
}

/**
 * Busca un sub-bloque `tag` dentro de un payload (bloques TAG ASCII + size u32 BE anidados).
 * \param payload buffer donde buscar (payload de un bloque que contiene sub-bloques).
 * \param tag etiqueta de 4 caracteres (p. ej. "CDHD").
 * \return payload del sub-bloque (sin cabecera) o null.
 */
export function findBlock(payload, tag) {
	if (!payload) return null;
	let off = 0;
	while (off + 8 <= payload.length) {
		const t = payload.toString('latin1', off, off + 4);
		const size = payload.readUInt32BE(off + 4);
		if (size < 8 || off + size > payload.length) return null;
		if (t === tag) return payload.subarray(off + 8, off + size);
		off += size;
	}
	return null;
}

/**
 * Decodifica un objeto de sala desde un payload `OBCD` (contiene `CDHD` + `VERB`).
 * Las coordenadas salen en píxeles (×8, como `resetRoomObject`, `object.cpp:1146`).
 * \param obcd payload del bloque OBCD.
 * \return { id, x, y, width, height, flags, parent, walkX, walkY, actordir } o null.
 */
export function decodeObject(obcd) {
	const cdhd = findBlock(obcd, 'CDHD');
	if (!cdhd || cdhd.length < CDHD_V5_SIZE) return null;
	return {
		id: cdhd.readUInt16LE(0),
		x: cdhd[2] * 8,
		y: cdhd[3] * 8,
		width: cdhd[4] * 8,
		height: cdhd[5] * 8,
		flags: cdhd[6],
		parent: cdhd[7],
		walkX: cdhd.readInt16LE(8),
		walkY: cdhd.readInt16LE(10),
		actordir: cdhd[12],
	};
}

/**
 * Decodifica la cabecera `IMHD` de un payload `OBIM` (id, número de imágenes y, en las
 * variantes de 78 B, la lista de hotspots).
 * \param obim payload del bloque OBIM.
 * \return { id, imageCount, hotspots } o null. `hotspots` va vacío en V5 PC (16 B sin lista).
 */
export function decodeObjectImage(obim) {
	const imhd = findBlock(obim, 'IMHD');
	if (!imhd || imhd.length < IMHD_V5_SIZE) return null;
	const hotspots = [];
	if (imhd.length >= IMHD_OLD_SIZE) {
		const n = Math.min(imhd.readUInt16LE(16), 15);
		for (let i = 0; i < n; i++) {
			hotspots.push({ x: imhd.readInt16LE(18 + i * 4), y: imhd.readInt16LE(20 + i * 4) });
		}
	}
	return {
		id: imhd.readUInt16LE(0),
		imageCount: imhd.readUInt16LE(2),
		hotspots,
	};
}

/**
 * Busca el sub-bloque de imagen de un objeto (`IMxx`, xx = estado en hex) dentro de su OBIM.
 * \param obim payload del bloque OBIM.
 * \param state estado pedido (por defecto 1; el dibujo por defecto de los objetos V5).
 * \return payload del sub-bloque de imagen o null (probando `IMxx`, `IM00` y el primer `IMxx`).
 */
export function findImageEntry(obim, state = 1) {
	if (!obim) return null;
	const wanted = `IM${state.toString(16).toUpperCase().padStart(2, '0')}`;
	let first = null;
	let off = 0;
	while (off + 8 <= obim.length) {
		const t = obim.toString('latin1', off, off + 4);
		const size = obim.readUInt32BE(off + 4);
		if (size < 8 || off + size > obim.length) break;
		if (t === wanted) return obim.subarray(off + 8, off + size);
		if (first === null && /^IM[0-9A-F]{2}$/.test(t) && t !== 'IMHD') first = obim.subarray(off + 8, off + size);
		off += size;
	}
	return first;
}

/**
 * Decodifica la paleta de una sala (CLUT o EPAL) al formato del SIM.
 * CLUT: 256 entradas × 3 bytes (RGB tal cual los guarda el juego; VGA usa 6 bits por canal).
 * EPAL: 256 bytes; cada byte = par de índices EGA (nibble bajo = color A, alto = color B).
 * \param clutPayload payload del bloque CLUT (o null).
 * \param epalPayload payload del bloque EPAL (o null).
 * \return { kind:"clut", colors:[[r,g,b]…] } | { kind:"epal", entries:[...] } | null.
 */
export function decodePalette(clutPayload, epalPayload) {
	if (clutPayload && clutPayload.length >= 768) {
		const colors = [];
		for (let i = 0; i < 256; i++) colors.push([clutPayload[i * 3], clutPayload[i * 3 + 1], clutPayload[i * 3 + 2]]);
		return { kind: 'clut', colors };
	}
	if (epalPayload && epalPayload.length >= 256) {
		return { kind: 'epal', entries: [...epalPayload.subarray(0, 256)] };
	}
	return null;
}

/**
 * Decodifica `RMHD` (cabecera de sala): ancho y alto en píxeles (`room.cpp:343`, u16 LE).
 * \param rmhd payload del bloque RMHD.
 * \return { width, height } o null.
 */
export function decodeRoomHeader(rmhd) {
	if (!rmhd || rmhd.length < 4) return null;
	return { width: rmhd.readUInt16LE(0), height: rmhd.readUInt16LE(2) };
}
