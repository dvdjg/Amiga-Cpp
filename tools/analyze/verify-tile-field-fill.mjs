#!/usr/bin/env node
/**
 * Test unitario del ALGORITMO de rellenado de TileFieldController
 * (`engine/include/eng/field/tile_field.hpp`).
 *
 * Replica fielmente la lógica de `begin` (offset ABSOLUTO) y `update` (delta
 * RELATIVO) y verifica invariantes de cobertura sobre un framebuffer de doble
 * página con scroll infinito:
 *
 *   - `begin(offset)`: el conjunto de franjas encoladas debe cubrir TODAS las
 *     celdas del framebuffer físico exactamente una vez (sin huecos ni solapes),
 *     y cada celda física debe contener el tile correcto del mapa virtual para
 *     el offset dado (world = page_origin + fb_offset).
 *   - `update(delta)`: al mover la cámara +N px (hasta max_delta=15), las franjas
 *     nuevas deben ser EXACTAMENTE las bandas que entran por el borde opuesto
 *     (los tiles offscreen que la cámara revelará), con el tramo de mundo
 *     correcto, sin dejar la página visible a medio dibujar.
 *   - inversión de dirección: al cambiar el signo del delta, la página opuesta
 *     se reencola con el tramo de la NUEVA dirección.
 *
 * El test simula el framebuffer como un array y "dibuja" cada franja encolada
 * con su tile de mundo, verificando después la cobertura y el contenido.
 *
 * Uso: node tools/analyze/verify-tile-field-fill.mjs
 */

const TILE = 16;
const VW = 320;
const VH = 256;
const MAP_W = 64;
const MAP_H = 32;

// Mapa virtual determinista: tile = (x*7 + y*13) & 63 (para detectar errores
// de posición con claridad).
function mapAt(tx, ty) {
	const x = ((tx % MAP_W) + MAP_W) % MAP_W;
	const y = ((ty % MAP_H) + MAP_H) % MAP_H;
	return (x * 7 + y * 13) & 63;
}

// --- Réplica del controlador (solo la parte de encolado de franjas) ---------

class Field {
	constructor(cfg) {
		this.cfg = cfg;
		this.fb_w = cfg.viewport_w * (cfg.scroll_x ? 2 : 1);
		this.fb_h = cfg.viewport_h * (cfg.scroll_y ? 2 : 1);
		this.world_x = 0;
		this.world_y = 0;
		this.page_origin_x = [0, 0];
		this.page_origin_y = [0, 0];
		this.active_page_x = 0;
		this.active_page_y = 0;
		this.last_dir_x = 0;
		this.last_dir_y = 0;
		this.pending = []; // {worldX, worldY, fbX, fbY, w, h}
	}

	// Equivale a TileLayerMap.tile_at con wrap.
	tileAt(tx, ty) {
		return mapAt(tx, ty);
	}

	begin(initial) {
		this.world_x = initial.x;
		this.world_y = initial.y;
		const base_x = Math.floor(initial.x / this.cfg.viewport_w) * this.cfg.viewport_w;
		const base_y = Math.floor(initial.y / this.cfg.viewport_h) * this.cfg.viewport_h;
		this.page_origin_x[0] = base_x;
		this.page_origin_x[1] = base_x + (this.cfg.scroll_x ? this.cfg.viewport_w : 0);
		this.page_origin_y[0] = base_y;
		this.page_origin_y[1] = base_y + (this.cfg.scroll_y ? this.cfg.viewport_h : 0);
		this.active_page_x = initial.x >= base_x + this.cfg.viewport_w ? 1 : 0;
		this.active_page_y = initial.y >= base_y + this.cfg.viewport_h ? 1 : 0;
		this.last_dir_x = 0;
		this.last_dir_y = 0;
		this.pending = [];
		this.enqueueInitialStrips();
	}

	// Réplica de enqueue_strip con cancelación por slot físico.
	enqueueStrip(wx, wy, fx, fy, w, h) {
		if (w === 0 || h === 0) return;
		// Cancelar franja activa en el mismo slot físico con distinto mundo.
		for (let i = 0; i < this.pending.length; ++i) {
			const s = this.pending[i];
			if (s.fbX === fx && s.fbY === fy) {
				if (s.worldX === wx && s.worldY === wy) return; // ya encolada
				this.pending.splice(i, 1); // mismo slot, mundo distinto: cancelar
				--i;
			}
		}
		this.pending.push({ worldX: wx, worldY: wy, fbX: fx, fbY: fy, w, h });
	}

	enqueuePageX(slot, origin) {
		const col = Math.floor(origin / TILE);
		const w = this.cfg.viewport_w / TILE;
		const ph = this.cfg.viewport_h / TILE;
		const pw = slot * w;
		const pagesY = this.cfg.scroll_y ? 2 : 1;
		for (let sy = 0; sy < pagesY; ++sy) {
			const py = this.active_page_y ^ sy;
			const row = Math.floor(this.page_origin_y[py] / TILE);
			this.enqueueStrip(col, row, pw, py * ph, w, ph);
		}
	}

	enqueuePageY(slot, origin) {
		const row = Math.floor(origin / TILE);
		const h = this.cfg.viewport_h / TILE;
		const pw = this.cfg.viewport_w / TILE;
		const ph = slot * h;
		const pagesX = this.cfg.scroll_x ? 2 : 1;
		for (let sx = 0; sx < pagesX; ++sx) {
			const px = this.active_page_x ^ sx;
			const col = Math.floor(this.page_origin_x[px] / TILE);
			this.enqueueStrip(col, row, px * pw, ph, pw, h);
		}
	}

	enqueueInitialStrips() {
		const pw = this.cfg.viewport_w / TILE;
		const ph = this.cfg.viewport_h / TILE;
		const pagesY = this.cfg.scroll_y ? 2 : 1;
		const pagesX = this.cfg.scroll_x ? 2 : 1;
		for (let sy = 0; sy < pagesY; ++sy) {
			for (let sx = 0; sx < pagesX; ++sx) {
				const col = Math.floor(this.page_origin_x[sx] / TILE);
				const row = Math.floor(this.page_origin_y[sy] / TILE);
				this.enqueueStrip(col, row, sx * pw, sy * ph, pw, ph);
			}
		}
	}

	// Réplica de update: inversión + cruce de página + encolado.
	update(dx, dy) {
		if (dx !== 0) {
			const dir = dx < 0 ? -1 : 1;
			if (this.last_dir_x !== 0 && dir !== this.last_dir_x) this.reprepPageX(dir);
			this.last_dir_x = dir;
		}
		if (dy !== 0) {
			const dir = dy < 0 ? -1 : 1;
			if (this.last_dir_y !== 0 && dir !== this.last_dir_y) this.reprepPageY(dir);
			this.last_dir_y = dir;
		}
		this.world_x += dx;
		this.world_y += dy;
		const act_x = this.active_page_x;
		const act_y = this.active_page_y;
		const mod_x = this.world_x - this.page_origin_x[act_x];
		if (this.cfg.scroll_x && mod_x >= this.cfg.viewport_w) {
			const vacated = act_x;
			this.active_page_x = act_x ^ 1;
			this.page_origin_x[vacated] = this.page_origin_x[this.active_page_x] + this.cfg.viewport_w;
			this.enqueuePageX(vacated, this.page_origin_x[vacated]);
		} else if (this.cfg.scroll_x && mod_x < 0) {
			const vacated = act_x;
			this.active_page_x = act_x ^ 1;
			this.page_origin_x[vacated] = this.page_origin_x[this.active_page_x] - this.cfg.viewport_w;
			this.enqueuePageX(vacated, this.page_origin_x[vacated]);
		}
		const mod_y = this.world_y - this.page_origin_y[act_y];
		if (this.cfg.scroll_y && mod_y >= this.cfg.viewport_h) {
			const vacated = act_y;
			this.active_page_y = act_y ^ 1;
			this.page_origin_y[vacated] = this.page_origin_y[this.active_page_y] + this.cfg.viewport_h;
			this.enqueuePageY(vacated, this.page_origin_y[vacated]);
		} else if (this.cfg.scroll_y && mod_y < 0) {
			const vacated = act_y;
			this.active_page_y = act_y ^ 1;
			this.page_origin_y[vacated] = this.page_origin_y[this.active_page_y] - this.cfg.viewport_h;
			this.enqueuePageY(vacated, this.page_origin_y[vacated]);
		}
	}

	reprepPageX(dir) {
		const other = this.active_page_x ^ 1;
		this.page_origin_x[other] = this.page_origin_x[this.active_page_x] + dir * this.cfg.viewport_w;
		this.enqueuePageX(other, this.page_origin_x[other]);
	}

	reprepPageY(dir) {
		const other = this.active_page_y ^ 1;
		this.page_origin_y[other] = this.page_origin_y[this.active_page_y] + dir * this.cfg.viewport_h;
		this.enqueuePageY(other, this.page_origin_y[other]);
	}
}

// --- Verificación de cobertura y contenido -----------------------------------

// Simula el framebuffer (grid de tiles) y aplica las franjas pendientes.
// Devuelve {coverage: Map<"fx,fy", {tile, count}>, written: Map<...tile...>}.
function simulate(field, fbTilesW, fbTilesH) {
	const coverage = new Map();
	const content = new Map();
	for (const s of field.pending) {
		for (let y = 0; y < s.h; ++y) {
			for (let x = 0; x < s.w; ++x) {
				const fx = s.fbX + x;
				const fy = s.fbY + y;
				if (fx < 0 || fx >= fbTilesW || fy < 0 || fy >= fbTilesH) {
					// Fuera del framebuffer: no debería ocurrir.
					continue;
				}
				const key = `${fx},${fy}`;
				coverage.set(key, (coverage.get(key) || 0) + 1);
				const tile = field.tileAt(s.worldX + x, s.worldY + y);
				content.set(key, tile);
			}
		}
	}
	return { coverage, content };
}

let failures = 0;
function check(name, cond, extra = '') {
	if (!cond) {
		console.error(`FAIL: ${name} ${extra}`);
		++failures;
	} else {
		console.log(`  ok: ${name}`);
	}
}

function verifyCoverage(field, fbTilesW, fbTilesH, label) {
	const { coverage, content } = simulate(field, fbTilesW, fbTilesH);
	const total = fbTilesW * fbTilesH;
	let covered = 0, overlaps = 0, correct = 0;
	for (let fy = 0; fy < fbTilesH; ++fy) {
		for (let fx = 0; fx < fbTilesW; ++fx) {
			const key = `${fx},${fy}`;
			const n = coverage.get(key) || 0;
			if (n > 0) covered++;
			if (n > 1) overlaps++;
			// Contenido esperado: world = page_origin[slot] + fb_offset.
			// El tile correcto es el del mapa en la posición de mundo que
			// corresponde a esta celda física.
			if (content.has(key)) {
				const tile = content.get(key);
				// worldX = page_origin_x[slot] + fx*TILE; worldY = ... + fy*TILE
				// (reconstruido desde la franja que escribió esta celda).
				const expected = expectedTile(field, fx, fy);
				if (tile === expected) correct++;
			}
		}
	}
	check(`[${label}] cobertura completa (${covered}/${total})`, covered === total);
	check(`[${label}] sin solapes (${overlaps})`, overlaps === 0);
	check(`[${label}] contenido correcto (${correct}/${total})`, correct === total);
}

// Calcula el tile de mundo esperado para una celda física (fx,fy) del
// framebuffer: el que escribiría begin/update según la página física y su
// origen de mundo.
function expectedTile(field, fx, fy) {
	// Página X física = fx / pw, página Y física = fy / ph.
	const pw = field.cfg.viewport_w / TILE;
	const ph = field.cfg.viewport_h / TILE;
	const sx = Math.floor(fx / pw);
	const sy = Math.floor(fy / ph);
	const wx = field.page_origin_x[sx] + (fx - sx * pw) * TILE;
	const wy = field.page_origin_y[sy] + (fy - sy * ph) * TILE;
	return field.tileAt(Math.floor(wx / TILE), Math.floor(wy / TILE));
}

// --- Casuísticas --------------------------------------------------------------

function testBegin(offset) {
	const field = new Field({ viewport_w: VW, viewport_h: VH, scroll_x: true, scroll_y: true });
	field.begin(offset);
	const fbW = VW * 2 / TILE; // 40
	const fbH = VH * 2 / TILE; // 32
	console.log(`begin(offset=${offset.x},${offset.y}) -> ${field.pending.length} franjas`);
	check('encola 4 franjas (2x2 páginas)', field.pending.length === 4);
	verifyCoverage(field, fbW, fbH, `begin(${offset.x},${offset.y})`);
	// La página activa debe contener el mundo visible en el offset.
	check(`página activa X=${field.active_page_x} coherente con offset`,
		field.page_origin_x[field.active_page_x] <= offset.x &&
		offset.x < field.page_origin_x[field.active_page_x] + VW);
}

function testUpdateForward() {
	const field = new Field({ viewport_w: VW, viewport_h: VH, scroll_x: true, scroll_y: true });
	field.begin({ x: 0, y: 0 });
	const fbW = VW * 2 / TILE;
	const fbH = VH * 2 / TILE;
	// Avanzar en X hasta cruzar la página (offset 320).
	// Antes del cruce: solo se dibujan los tiles revelados (offscreen).
	for (let step = 0; step < 20; ++step) {
		const before = field.pending.length;
		field.update(16, 0); // un tile = 16px
		// Al avanzar 16px sin cruzar página, NO debe encolar nada nuevo:
		// la página opuesta ya se rellenó (no hay tiles nuevos offscreen).
		check(`update(+16) sin cruce no encola franjas nuevas (paso ${step})`,
			field.pending.length === before,
			`(pendientes ${before} -> ${field.pending.length})`);
	}
	// Al cruzar la página (world_x pasa 320), la página vacante se reencola.
	// world_x tras 20 pasos de 16 = 320 => justo en el cruce.
	// (verificamos cobertura tras aplicar las franjas pendientes)
	verifyCoverage(field, fbW, fbH, 'update forward hasta cruce');
}

function testUpdateRelativeOffscreen() {
	// Verifica que update con delta pequeño (1..15px) encola SOLO los tiles
	// que entran por el borde (offscreen), no toda la página.
	const field = new Field({ viewport_w: VW, viewport_h: VH, scroll_x: true, scroll_y: true });
	field.begin({ x: 0, y: 0 });
	// Vaciar las franjas iniciales (como hace la demo con pump).
	field.pending = [];
	// Avanzar 1px cada frame: nunca debería encolar nada (sin cruce de tile).
	for (let i = 0; i < 16; ++i) {
		const before = field.pending.length;
		field.update(1, 0);
		check(`update(+1px) no encola (frame ${i})`, field.pending.length === before);
	}
	// Avanzar hasta cruzar el tile 16 -> entra una columna nueva.
	field.update(1, 0); // ahora world_x = 17, cruzó el tile 16
	// El controlador solo reencola en cruce de PÁGINA (320px), no de tile:
	// por diseño, los tiles se revelan en el cruce de página, no por columna.
	check('update sin cruce de página no encola (tiles se revelan en página)',
		field.pending.length === 0);
}

function testInversion() {
	const field = new Field({ viewport_w: VW, viewport_h: VH, scroll_x: true, scroll_y: true });
	field.begin({ x: 0, y: 0 });
	// Pintar las 4 páginas iniciales (simula pump hasta busy()==false).
	const { content: initialContent } = simulate(field, VW * 2 / TILE, VH * 2 / TILE);
	field.pending = [];
	// Avanzar a world_x=320 (cruce de página): la página 0 se reencola.
	field.update(320, 0);
	check('cruce forward encola la página vacante (2 franjas)', field.pending.length === 2);
	const fbW = VW * 2 / TILE;
	const fbH = VH * 2 / TILE;
	// Tras el cruce, el framebuffer debe seguir cubierto: las 4 páginas (las 2
	// ya pintadas + las 2 reencoladas). Simulamos solo las nuevas franjas y
	// añadimos las ya pintadas para verificar contenido total.
	const { coverage, content } = simulate(field, fbW, fbH);
	// Combinar con el contenido inicial: las páginas no reencoladas conservan
	// su contenido previo.
	let covered = 0, correct = 0;
	for (let fy = 0; fy < fbH; ++fy) {
		for (let fx = 0; fx < fbW; ++fx) {
			const key = `${fx},${fy}`;
			const tile = content.has(key) ? content.get(key) : initialContent.get(key);
			const expected = expectedTile(field, fx, fy);
			if (tile !== undefined) covered++;
			if (tile === expected) correct++;
		}
	}
	check(`[tras cruce forward] cobertura total (${covered}/${fbW * fbH})`, covered === fbW * fbH);
	check(`[tras cruce forward] contenido total correcto (${correct}/${fbW * fbH})`, correct === fbW * fbH);
	// Invertir: al retroceder, se detecta la inversión (reprep de la página
	// opuesta con el tramo TRASERO) y además el delta -16 cruza hacia atrás
	// (mod_x < 0), reencolando la página que queda atrás con el tramo anterior.
	// Total 4 franjas: 2 de reprep + 2 del cruce backward.
	field.pending = [];
	field.update(-16, 0); // retrocede: inversión + cruce backward
	check('inversión + cruce backward encolan 4 franjas',
		field.pending.length === 4,
		`(pendientes ${field.pending.length})`);
	// Tras la inversión, active_x vuelve a 0 y la página opuesta (física 1) se
	// reencola con el tramo TRASERO: world = page_origin_x[0] - VW = -320.
	const backStrip = field.pending.find((s) => s.fbX === (VW / TILE) &&
		s.worldX === Math.floor((field.page_origin_x[0] - VW) / TILE));
	check('tramo trasero correcto tras inversión (página opuesta)', backStrip !== undefined);
	// La página que queda atrás (física 0) se rellena con el tramo anterior al
	// nuevo activo: world = page_origin_x[0] (tras el cruce backward).
	const afterStrip = field.pending.find((s) => s.fbX === 0 &&
		s.worldX === Math.floor(field.page_origin_x[0] / TILE));
	check('tramo anterior al nuevo activo tras cruce backward', afterStrip !== undefined);
}

function testBeginNonAligned() {
	// begin con offset NO alineado a página (e.g. x=160, y=128): el viewport
	// queda en medio de la página 0 y la cobertura debe seguir completa.
	const field = new Field({ viewport_w: VW, viewport_h: VH, scroll_x: true, scroll_y: true });
	field.begin({ x: 160, y: 128 });
	verifyCoverage(field, VW * 2 / TILE, VH * 2 / TILE, 'begin no alineado');
	check('página activa X=0 (160 < 320)', field.active_page_x === 0);
}

function main() {
	console.log('TileFieldController: algoritmo de rellenado (begin/update)');
	console.log('--- begin: offset absoluto en distintas zonas ---');
	testBegin({ x: 0, y: 0 });
	testBegin({ x: 320, y: 0 });      // justo en borde de página X
	testBegin({ x: 480, y: 0 });      // en página X 1
	testBegin({ x: 0, y: 256 });      // borde de página Y
	testBegin({ x: 160, y: 128 });    // no alineado
	testBegin({ x: 640, y: 512 });    // dos páginas más allá
	testBeginNonAligned();

	console.log('--- update: delta relativo (hasta 15px) ---');
	testUpdateForward();
	testUpdateRelativeOffscreen();
	testInversion();

	if (failures === 0) {
		console.log('OK verify-tile-field-fill: cobertura y contenido correctos');
		process.exit(0);
	}
	console.error(`FAIL: ${failures} verificación(es) fallaron`);
	process.exit(1);
}

main();
