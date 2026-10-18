// Validador del **SIM** (Semantic Intermediate Model): la salida de extracción que consumen
// los siguientes pasos del pipeline (traducción, cocina, preview). Versiones:
//   - `sim/1`: inventario de extracción (salas y bloques) — ver SCHEMA.md.
//   - `sim/2`: contenido semántico (tamaño de sala, walkboxes + adyacencia, paleta, objetos
//     con hotspots de imagen y guiones por sala) — producido por `tools/adventure/decode/`.
// Puro y sin dependencias: lo usan el extractor, los decodificadores y los tests del pipeline.

/**
 * Valida un SIM de cualquier versión soportada.
 * \param sim objeto parseado.
 * \return lista de errores (vacía = válido).
 */
export function validateSim(sim) {
	if (sim !== null && typeof sim === 'object' && sim.format === 'sim/2') return validateSim2(sim);
	return validateSim1(sim);
}

/**
 * Valida un SIM `sim/1` (inventario de extracción).
 * \param sim objeto parseado.
 * \return lista de errores (vacía = válido).
 */
export function validateSim1(sim) {
	const errors = [];
	const req = (cond, msg) => {
		if (!cond) errors.push(msg);
	};
	req(sim !== null && typeof sim === 'object', 'sim: objeto requerido');
	if (sim === null || typeof sim !== 'object') return errors;
	req(sim.format === 'sim/1', `format debe ser "sim/1" (es ${JSON.stringify(sim.format)})`);
	const g = sim.game ?? {};
	req(typeof g.id === 'string' && g.id.length > 0, 'game.id requerido');
	req(g.source !== null && typeof g.source === 'object', 'game.source requerido');
	req(typeof g.source?.type === 'string', 'game.source.type requerido');
	req(Array.isArray(g.source?.data), 'game.source.data[] requerido');
	req(Number.isInteger(g.source?.enc), 'game.source.enc entero (clave XOR; 0 = sin cifrado)');
	req(g.counts !== null && typeof g.counts === 'object', 'game.counts requerido');
	for (const k of ['rooms', 'scripts', 'costumes', 'sounds', 'charsets', 'globalObjects']) {
		req(Number.isInteger(g.counts?.[k]), `game.counts.${k} entero`);
	}
	req(sim.roomNames !== null && typeof sim.roomNames === 'object', 'roomNames objeto');
	req(Array.isArray(sim.rooms), 'rooms[] requerido');
	const seen = new Set();
	(sim.rooms ?? []).forEach((r, i) => {
		const at = `rooms[${i}]`;
		req(Number.isInteger(r.room) && r.room > 0, `${at}.room entero > 0`);
		req(!seen.has(r.room), `${at}.room duplicado (${r.room})`);
		seen.add(r.room);
		req(typeof r.name === 'string', `${at}.name string`);
		req(typeof r.file === 'string' && r.file.length > 0, `${at}.file requerido`);
		req(Number.isInteger(r.off) && r.off >= 0, `${at}.off entero >= 0`);
		for (const key of ['blocks', 'lflfBlocks', 'scripts']) {
			req(Array.isArray(r[key]), `${at}.${key} requerido`);
			for (const b of r[key] ?? []) {
				req(typeof b.tag === 'string' && b.tag.length === 4, `${at}.${key}[] tag de 4 caracteres`);
				req(Number.isInteger(b.size) && b.size >= 8, `${at}.${key}[] size entero >= 8`);
			}
		}
		for (const b of r.scripts ?? []) {
			req(Number.isInteger(b.off) && b.off >= 0, `${at}.scripts[] off entero >= 0`);
		}
	});
	return errors;
}

/**
 * Valida un SIM `sim/2` (contenido semántico decodificado).
 * \param sim objeto parseado.
 * \return lista de errores (vacía = válido).
 */
export function validateSim2(sim) {
	const errors = [];
	const req = (cond, msg) => {
		if (!cond) errors.push(msg);
	};
	req(sim.format === 'sim/2', `format debe ser "sim/2" (es ${JSON.stringify(sim.format)})`);
	const g = sim.game ?? {};
	req(typeof g.id === 'string' && g.id.length > 0, 'game.id requerido');
	req(typeof g.engine === 'string' && g.engine.length > 0, 'game.engine requerido');
	req(Array.isArray(sim.rooms) && sim.rooms.length > 0, 'rooms[] no vacío requerido');
	const isPoint = (p) => Array.isArray(p) && p.length === 2 && p.every(Number.isFinite);
	const seen = new Set();
	for (const r of sim.rooms ?? []) {
		const at = `rooms[${r.room}]`;
		req(Number.isInteger(r.room) && r.room > 0, `${at}.room entero > 0`);
		req(!seen.has(r.room), `${at}.room duplicado (${r.room})`);
		seen.add(r.room);
		req(typeof r.name === 'string', `${at}.name string`);
		req(Number.isInteger(r.width) && r.width > 0 && Number.isInteger(r.height) && r.height > 0, `${at}: width/height enteros > 0`);
		req(Array.isArray(r.walkboxes), `${at}.walkboxes requerido`);
		for (const b of r.walkboxes ?? []) {
			req(Number.isInteger(b.id), `${at}.walkboxes[] id entero`);
			for (const k of ['ul', 'ur', 'lr', 'll']) req(isPoint(b[k]), `${at}.walkboxes[] ${k} = [x,y]`);
			req(Number.isInteger(b.mask) && Number.isInteger(b.flags) && Number.isInteger(b.scale),
			    `${at}.walkboxes[] mask/flags/scale enteros`);
		}
		req(r.adjacency === null || (Array.isArray(r.adjacency) && r.adjacency.every((run) => Array.isArray(run))),
		    `${at}.adjacency null o array de runs`);
		if (r.palette !== null) {
			req(r.palette.kind === 'clut' || r.palette.kind === 'epal', `${at}.palette.kind clut|epal`);
			if (r.palette.kind === 'clut') req(Array.isArray(r.palette.colors) && r.palette.colors.length === 256, `${at}.palette.colors = 256`);
			if (r.palette.kind === 'epal') req(Array.isArray(r.palette.entries) && r.palette.entries.length === 256, `${at}.palette.entries = 256`);
		}
		req(Array.isArray(r.objects), `${at}.objects requerido`);
		for (const o of r.objects ?? []) {
			req(Number.isInteger(o.id), `${at}.objects[] id entero`);
			for (const k of ['x', 'y', 'width', 'height', 'flags', 'parent', 'walkX', 'walkY', 'actordir']) {
				req(Number.isInteger(o[k]), `${at}.objects[] ${k} entero`);
			}
			if (o.image !== undefined) {
				req(Number.isInteger(o.image.imageCount), `${at}.objects[].image.imageCount entero`);
				req(Array.isArray(o.image.hotspots), `${at}.objects[].image.hotspots requerido`);
				req(o.image.width === undefined || Number.isInteger(o.image.width), `${at}.objects[].image.width entero si existe`);
				req(o.image.height === undefined || Number.isInteger(o.image.height), `${at}.objects[].image.height entero si existe`);
			}
		}
		req(Array.isArray(r.scripts), `${at}.scripts requerido`);
		for (const s of r.scripts ?? []) {
			req(typeof s.tag === 'string' && s.tag.length === 4, `${at}.scripts[] tag`);
			req(Number.isInteger(s.size) && s.size >= 8, `${at}.scripts[] size >= 8`);
			req(typeof s.file === 'string' && s.file.length > 0, `${at}.scripts[] file`);
		}
	}
	return errors;
}
