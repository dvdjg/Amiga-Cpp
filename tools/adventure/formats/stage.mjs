// Formato **`adv-stage/1`**: descripción de sala **orientada al hardware Amiga** para las
// aventuras hechas ex-profeso para A500. Extiende el SIM (geometría/contenido) con directivas
// de presentación y efectos: display, capas y scroll por hardware, parallax, transparencias,
// **Copper Score** (cues con disparadores), canales de sprite, BOBs, HUD, streaming y
// presupuesto declarado. Validación pura; el compositor (A3) lo bajará a `eng::adv`/`eng::scene`.
//
// Referencias del engine: `scene::LayerPlan`/`SceneStrategy` (`scene/plan.hpp`),
// `scene::ScrollKind`/`ScrollSpec`/`RegionCost` (`scene/world.hpp`), `copper::Plan` y
// `eng::setup::check_copper_list`, `eng::hud` (`hud/region.hpp`), `eng::hw::bus_budget`.
// El documento canónico es `STAGE_FORMAT.md` (esta implementación y él deben ir en paralelo).

export const STAGE_FORMAT = 'adv-stage/1';

export const DISPLAY_MODES = new Set(['single', 'dpf', 'ehb', 'ham']);
export const LAYER_KINDS = new Set(['pfFar', 'pfNear', 'sprLayer', 'bobLayer', 'panel', 'fx', 'hud']);
export const SCROLL_KINDS = new Set(['none', 'fine', 'copperRing', 'blitterColumns', 'copperSplit', 'strip']);
export const TRANSPARENCY_KINDS = new Set(['none', 'colorKey', 'dpfPriority']);
export const TRIGGER_KINDS = new Set(['onEnter', 'onScrollX', 'onActorY', 'onDialogue', 'onBeat', 'onScript']);
export const TRACK_KINDS = new Set(['palette', 'reg', 'waitMarker']);
export const CONTENT_TYPES = new Set(['png', 'tiles', 'uaf']);
export const HUD_ELEMENTS = new Set(['verbBar', 'inventory', 'subtitles', 'dialogue', 'status']);

/**
 * Valida un documento `adv-stage/1`.
 * \param st objeto parseado.
 * \return lista de errores (vacía = válido).
 */
export function validateStage(st) {
	const errors = [];
	const req = (cond, msg) => {
		if (!cond) errors.push(msg);
	};
	const int = (v) => Number.isInteger(v);
	req(st && typeof st === 'object', 'stage: objeto requerido');
	if (!st || typeof st !== 'object') return errors;
	req(st.format === STAGE_FORMAT, `format debe ser "${STAGE_FORMAT}"`);
	req(int(st.room) && st.room > 0, 'room entero > 0');
	req(typeof st.name === 'string', 'name string');

	const d = st.display ?? {};
	req(DISPLAY_MODES.has(d.mode), 'display.mode single|dpf|ehb|ham');
	req(int(d.planes) && d.planes >= 1 && d.planes <= 6, 'display.planes 1..6 (A500)');
	req(int(d.height) && d.height >= 1 && d.height <= 256, 'display.height 1..256');
	if (d.mode === 'ehb') req(d.planes === 6, 'EHB exige 6 planos');
	if (d.mode === 'ham') req(d.planes === 6, 'HAM exige 6 planos');
	if (d.mode === 'dpf') req(d.planes <= 6, 'DPF: suma de planos de los dos campos ≤ 6');

	const pal = st.palette ?? {};
	req(int(pal.slots) && pal.slots >= 2 && pal.slots <= 32, 'palette.slots 2..32');
	req(Array.isArray(pal.protectedSlots), 'palette.protectedSlots[]');
	for (const s of pal.protectedSlots ?? []) req(int(s) && s >= 0 && s < (pal.slots ?? 0), `palette.protectedSlots fuera de rango (${s})`);
	req(typeof pal.managedByCopper === 'boolean', 'palette.managedByCopper bool');

	req(Array.isArray(st.layers) && st.layers.length > 0, 'layers[] no vacío');
	const ids = new Set();
	for (const L of st.layers ?? []) {
		const at = `layers[${L?.id ?? '?'}]`;
		req(typeof L.id === 'string' && L.id.length > 0, `${at}.id requerido`);
		req(!ids.has(L.id), `${at}.id duplicado`);
		ids.add(L.id);
		req(LAYER_KINDS.has(L.kind), `${at}.kind inválido`);
		const band = L.band ?? {};
		req(int(band.top) && int(band.height) && band.height > 0 && band.top + band.height <= 256, `${at}.band dentro de 256 líneas`);
		const c = L.content ?? {};
		req(CONTENT_TYPES.has(c.type), `${at}.content.type png|tiles|uaf`);
		req(typeof c.ref === 'string' && c.ref.length > 0, `${at}.content.ref requerido`);
		const sc = L.scroll ?? { kind: 'none' };
		req(SCROLL_KINDS.has(sc.kind), `${at}.scroll.kind inválido`);
		req(sc.mapPeriodWords === undefined || (int(sc.mapPeriodWords) && sc.mapPeriodWords >= 0), `${at}.scroll.mapPeriodWords ≥ 0`);
		req(sc.speedPx === undefined || (int(sc.speedPx) && sc.speedPx >= 0 && sc.speedPx <= 32), `${at}.scroll.speedPx 0..32`);
		req(int(L.parallax) && L.parallax >= 0 && L.parallax <= 1024, `${at}.parallax 0..1024 (8.8, 256 = 1.0)`);
		const tr = L.transparency ?? { kind: 'none' };
		req(TRANSPARENCY_KINDS.has(tr.kind), `${at}.transparency.kind inválido`);
		req(int(L.field ?? 0) && (L.field ?? 0) >= 0 && (L.field ?? 0) <= 2, `${at}.field 0..2`);
	}

	const cu = st.copper ?? {};
	req(int(cu.budgetWordsPerLine) && cu.budgetWordsPerLine >= 0 && cu.budgetWordsPerLine <= 64, 'copper.budgetWordsPerLine 0..64 (política A500)');
	req(Array.isArray(cu.cues ?? []), 'copper.cues[]');
	for (const q of cu.cues ?? []) {
		const at = `copper.cues[${q?.id ?? '?'}]`;
		req(typeof q.id === 'string' && q.id.length > 0, `${at}.id requerido`);
		req(TRIGGER_KINDS.has(q.trigger?.kind), `${at}.trigger.kind inválido`);
		req(int(q.frames ?? 0) && (q.frames ?? 0) >= 0, `${at}.frames ≥ 0`);
		req(Array.isArray(q.tracks) && q.tracks.length > 0, `${at}.tracks[] no vacío`);
		for (const t of q.tracks ?? []) {
			req(TRACK_KINDS.has(t.kind), `${at}.tracks[].kind inválido`);
			if (t.kind === 'palette') {
				req(int(t.first) && int(t.count) && t.first >= 0 && t.count > 0 && t.first + t.count <= 32, `${at}: rango de paleta 0..32`);
				req(Array.isArray(t.keyframes) && t.keyframes.length > 0, `${at}: keyframes[] no vacío`);
				for (const k of t.keyframes) {
					req(int(k.frame) && k.frame >= 0, `${at}: keyframe.frame ≥ 0`);
					req(Array.isArray(k.colors) && k.colors.length === t.count, `${at}: keyframe con ${t.count} colores`);
				}
			}
			if (t.kind === 'reg') {
				req(int(t.line) && t.line >= 0 && t.line <= 255, `${at}: reg.line 0..255`);
				req(typeof t.reg === 'string' && t.reg.length > 0, `${at}: reg.reg requerido`);
				req(Number.isInteger(t.value), `${at}: reg.value entero`);
			}
			if (t.kind === 'waitMarker') req(int(t.line) && t.line >= 0 && t.line <= 255, `${at}: waitMarker.line`);
		}
	}

	const sp = st.sprites ?? {};
	req(int(sp.maxChannels ?? 8) && (sp.maxChannels ?? 8) >= 1 && (sp.maxChannels ?? 8) <= 8, 'sprites.maxChannels 1..8 (A500)');
	const channels = new Set();
	for (const ch of sp.channels ?? []) {
		req(int(ch.channel) && ch.channel >= 0 && ch.channel < (sp.maxChannels ?? 8), `sprites.channels[].channel 0..${(sp.maxChannels ?? 8) - 1}`);
		req(!channels.has(ch.channel), `sprites.channels[].channel duplicado (${ch.channel})`);
		channels.add(ch.channel);
		req(typeof ch.role === 'string' && ch.role.length > 0, 'sprites.channels[].role requerido');
	}

	const bobs = st.bobs ?? {};
	req(bobs.max === undefined || (int(bobs.max) && bobs.max >= 0), 'bobs.max ≥ 0');
	req(bobs.budgetWordsPerFrame === undefined || (int(bobs.budgetWordsPerFrame) && bobs.budgetWordsPerFrame >= 0), 'bobs.budgetWordsPerFrame ≥ 0');

	const hud = st.hud ?? null;
	if (hud !== null) {
		req(int(hud.region?.top) && int(hud.region?.height) && hud.region.height > 0 && hud.region.top + hud.region.height <= 256, 'hud.region dentro de 256 líneas');
		req(Array.isArray(hud.elements ?? []), 'hud.elements[]');
		for (const e of hud.elements ?? []) req(HUD_ELEMENTS.has(e), `hud.elements[] inválido (${e})`);
	}

	const stream = st.stream ?? {};
	req(Array.isArray(stream.resident ?? []), 'stream.resident[]');
	req(Array.isArray(stream.roomPack ?? []), 'stream.roomPack[]');
	req(Array.isArray(stream.optional ?? []), 'stream.optional[]');

	const b = st.budget ?? {};
	for (const k of ['chipBytes', 'planes', 'copperWordsPerLine', 'blitterWordsPerFrame', 'spriteChannels']) {
		req(b[k] === undefined || (int(b[k]) && b[k] >= 0), `budget.${k} entero ≥ 0`);
	}
	return errors;
}

const hex = (n) => `#${n.toString(16).padStart(3, '0')}`;

/**
 * Genera un `adv-stage/1` inicial para una sala del SIM v2 (punto de partida editable).
 * Los valores son deliberadamente conservadores para A500; el editor los amplía (capas,
 * parallax, cues, sprites). Determinista.
 * \param room entrada `rooms[]` del SIM v2.
 * \param gameId id del juego (para las referencias de contenido).
 * \return documento `adv-stage/1`.
 */
export function scaffoldStage(room, gameId) {
	const palette = room.palette;
	const colors = [];
	if (palette?.kind === 'clut') {
		for (const c of palette.colors.slice(0, 16)) colors.push(hex(((c[0] >> 2) << 8) | ((c[1] >> 2) << 4) | (c[2] >> 2)));
	}
	return {
		format: STAGE_FORMAT,
		room: room.room,
		name: room.name ?? '',
		display: { mode: 'single', planes: 5, height: Math.min(room.height || 200, 256) },
		palette: { slots: 32, protectedSlots: [0, 1, 2, 3], managedByCopper: false, samples: colors },
		layers: [
			{
				id: 'bg',
				kind: 'pfNear',
				band: { top: 0, height: Math.min(room.height || 200, 256) },
				content: { type: 'png', ref: `decoded/png/rooms/room_${room.room}.png` },
				scroll: { kind: 'none', mapPeriodWords: 0, speedPx: 2 },
				parallax: 256,
				transparency: { kind: 'none', color: 0 },
				field: 0,
			},
		],
		copper: { budgetWordsPerLine: 16, cues: [] },
		sprites: { maxChannels: 8, channels: [{ channel: 0, role: 'cursor' }] },
		bobs: { max: 16, budgetWordsPerFrame: 0 },
		hud: { region: { top: Math.min((room.height || 200), 208), height: 48 }, elements: ['verbBar', 'inventory', 'subtitles'] },
		stream: { resident: [], roomPack: [], optional: [] },
		budget: { chipBytes: 0, planes: 5, copperWordsPerLine: 16, blitterWordsPerFrame: 0, spriteChannels: 8 },
		notes: [
			`Andamiaje generado desde el SIM v2 para ${gameId}; ajustar modo/planos (A500: single ≤5-6, EHB/HAM 6, DPF 3+3).`,
			palette?.kind === 'clut' ? `Paleta de origen CLUT (${colors.length} colores de muestra en este documento, ya en 4 bits).` : 'Sin paleta CLUT en el SIM: rellenar a mano.',
		],
	};
}
