// Validador del **SIM v1** (Semantic Intermediate Model): la salida de extracción que
// consumen los siguientes pasos del pipeline (traducción, cocina, preview). Ver SCHEMA.md.
//
// Puro y sin dependencias: lo usa el extractor (aviso temprano) y los tests host del pipeline.

export function validateSim(sim) {
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
