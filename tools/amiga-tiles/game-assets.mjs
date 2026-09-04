#!/usr/bin/env node
// game-assets.mjs — PIPELINE ÚNICO e inteligente de sprites/fondos para juegos.
// Con UNA orden hace todo el flujo que antes hacíamos a mano:
//   1. Detecta los fondos (varios colores por zona; o usa el alpha si ya hay).
//   2. Extrae sprites por componentes conexos (cada pieza con bbox/centro/ancla).
//   3. (opcional --split) separa sprites que se tocan (erosión 1 px).
//   4. Agrupa por identidad: heurística de rejilla + (si hay ollama local) json de
//      agrupación del modelo de visión; fallback 100% determinista si no hay IA.
//   5. Organiza en <out>/<job>/<grupo>/frame_NN_*.png + group.json (frames + offset
//      + ancla de alineación) y documenta la convención de transparencia.
//   6. (opcional --quantize N) cuantiza CADA GRUPO por separado: cada grupo de
//      frames tiene SU PROPIA paleta (en <grupo>/palette_*.json/.h) y todos sus
//      frames comparten esa misma paleta. N = 64 → EHB (32 bases + 32 half, índice
//      0 transparente); N = 2^n−1 (31,15,7…) → N colores + índice 0 transparente.
//
// La IA (ollama local, qwen3-vl) PARTICIPA en las decisiones de agrupar/nombrar y
// evaluar completitud; si no está disponible, el pipeline decide por heurística.
//
// Uso:
//   node tools/amiga-tiles/game-assets.mjs <imagen> [opciones]
//   --out DIR      (def: <imagen>.ass, o ./out/<job>)   --job NOMBRE
//   --tol N        tolerancia rgb de fondo (def 24)     --min N área mínima (def 30)
//   --split        separa sprites que se tocan (morfología ligera)
//   --ai           fuerza el paso de agrupación por ollama (def: auto si disponible)
//   --no-ai        fuerza heurística (sin llamar a ollama)
//   --tokens N     tokens máx de la IA (def 5000)
//   --quantize N   además cuantiza cada grupo por separado, con SU propia paleta
//                  compartida por sus frames; N=64 → EHB, N=2^n−1 → colores + índice 0
//                  transparente (se deduce: 31,15,7…)
//   --keep-all     no descartar piezas marcadas 'incompletas' por la IA
//
// Dónde se llama: es un CLI de entrada directa; internamente DELEGA en
//   ./extract-sprites.mjs (loadImage, contactSheet, ask, extractJson) y, en el paso
//   --quantize, lanza a su vez ./amiga-tiles.mjs como subproceso.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';
import { PNG } from 'pngjs';
import { loadImage, detectBackground, extract, cropSprite, contactSheet, ask, extractJson } from './extract-sprites.mjs';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(__dirname, '../..');
const argV = (n, d) => { const i = process.argv.indexOf(n); return i >= 0 && i + 1 < process.argv.length ? process.argv[i + 1] : d; };
const hasArg = (n) => process.argv.indexOf(n) >= 0;
const dist2 = (a, b) => { const dr = a[0] - b[0], dg = a[1] - b[1], db = a[2] - b[2]; return dr * dr + dg * dg + db * db; };
const lum = (c) => 0.299 * c[0] + 0.587 * c[1] + 0.114 * c[2];

// --- 1) Detección de fondos multi-zona --------------------------------------
// Recoge los colores dominantes en bordes y en bandas horizontales uniformes.
// Llamado desde main() (una vez por imagen; decide si hay transparencia 'alpha',
// varios colores 'colors' o nada 'none').
function detectBackgrounds(png, tol) {
	let alphaN = 0;
	for (let i = 3; i < png.data.length; i += 4) if (png.data[i] < 16) alphaN++;
	if (alphaN > png.width * png.height * 0.02) return { mode: 'alpha', bgs: [] };

	const W = png.width, H = png.height;
	// fams(points): histograma de los colores de una lista de píxeles, agrupado por
	// tolerancia (la "familia" del color dominante se cuenta junta).
	const fams = (points) => {
		const h = new Map();
		for (const [x, y] of points) { const o = (y * W + x) * 4; const k = (png.data[o] << 16) | (png.data[o + 1] << 8) | png.data[o + 2]; h.set(k, (h.get(k) || 0) + 1); }
		const e = [...h.entries()].sort((a, b) => b[1] - a[1]);
		const out = [];
		for (const [k, n] of e) {
			const c = [(k >> 16) & 255, (k >> 8) & 255, k & 255];
			if (out.some((b) => dist2(b.color, c) <= tol * tol)) { out[out.findIndex((b) => dist2(b.color, c) <= tol * tol)].count += n; continue; }
			out.push({ color: c, count: n });
		}
		return out.sort((a, b) => b.count - a.count);
	};
	// Bordes de la imagen (todo el marco + 2 bandas superior/inferior): aquí vive
	// casi siempre el color de fondo principal (cielo, etc.).
	const edges = [];
	for (let x = 0; x < W; x++) for (let y = 0; y < H; y += Math.max(1, H >> 3)) { if (y === 0 || y === H - 1) edges.push([x, y]); }
	for (let y = 0; y < H; y++) { edges.push([0, y]); edges.push([W - 1, y]); if (y < 2 || y >= H - 2) for (let x = 0; x < W; x++) edges.push([x, y]); }
	const edgeFams = fams(edges);
	const totalE = edgeFams.reduce((s, f) => s + f.count, 0);
	const bgs = [];
	for (const f of edgeFams) { if (f.color && lum(f.color) >= 40 && f.count / totalE >= 0.12) bgs.push(f.color); if (bgs.length >= 4) break; }
	// Bandas horizontales Y verticales uniformes (zonas separadas): distintos fondos.
	// scan('h') barre franjas horizontales, scan('v') verticales; cada franja cuyo
	// color dominante (familia) sea claro y uniforme se añade como fondo.
	const scan = (axis) => {
		const step = Math.max(8, Math.round((axis === 'h' ? H : W) / 24));
		for (let p0 = 0; p0 < (axis === 'h' ? H : W) && bgs.length < 4; p0 += step) {
			const pts = [];
			const p1 = Math.min(axis === 'h' ? H : W, p0 + step);
			if (axis === 'h') for (let y = p0; y < p1; y++) for (let x = 0; x < W; x += 2) pts.push([x, y]);
			else for (let x = p0; x < p1; x++) for (let y = 0; y < H; y += 2) pts.push([x, y]);
			const fb = fams(pts);
			const tot = fb.reduce((s, f) => s + f.count, 0);
			for (const f of fb) {
				if (f.count / tot < 0.22) break;
				if (lum(f.color) < 40) break;               // muy oscuro = contenido, no fondo
				if (bgs.some((b) => dist2(b, f.color) <= tol * tol)) continue;
				bgs.push(f.color);
				break;
			}
		}
	};
	scan('h'); scan('v');
	return { mode: bgs.length ? 'colors' : 'none', bgs: bgs.slice(0, 4) };
}
// Predicado "¿es fondo?" para la lista multi-color bgs. Llamado desde extractMulti,
// splitTouching y el main() al construir cada sprite (decidir alfa 0).
function isBgMulti(png, W, bgs, tol, x, y) {
	if (!bgs.length) return false;
	const o = (y * W + x) * 4, c = [png.data[o], png.data[o + 1], png.data[o + 2]];
	for (const b of bgs) if (dist2(c, b) <= tol * tol) return true;
	return false;
}
// Componentes conexos sobre máscara multi-fondo (adaptación de extract con listas).
// Flood-fill iterativo (sin recursión) sobre píxeles NO fondo; devuelve {label, comps}.
// Llamado desde main() cuando NO hay --split.
function extractMulti(png, bgs, tol, minArea) {
	const W = png.width, H = png.height;
	if (bgs.length === 0) return { comps: [{ minX: 0, minY: 0, maxX: W - 1, maxY: H - 1, area: W * H }], W, H };
	const isBg = (x, y) => isBgMulti(png, W, bgs, tol, x, y);
	const label = new Int32Array(W * H).fill(-1);
	const stack = new Int32Array(W * H * 2);
	const comps = [];
	let id = 0;
	for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) {
		if (label[y * W + x] !== -1) continue;
		if (isBg(x, y)) { label[y * W + x] = -2; continue; }
		let sp = 0; stack[sp++] = x; stack[sp++] = y; label[y * W + x] = id;
		let minX = x, minY = y, maxX = x, maxY = y, area = 0;
		while (sp > 0) { const cy = stack[--sp], cx = stack[--sp]; area++;
			if (cx < minX) minX = cx; if (cx > maxX) maxX = cx; if (cy < minY) minY = cy; if (cy > maxY) maxY = cy;
			for (const [dx, dy] of [[1, 0], [-1, 0], [0, 1], [0, -1]]) { const nx = cx + dx, ny = cy + dy;
				if (nx < 0 || ny < 0 || nx >= W || ny >= H) continue;
				if (label[ny * W + nx] !== -1) continue;
				if (isBg(nx, ny)) continue;
				label[ny * W + nx] = id; stack[sp++] = nx; stack[sp++] = ny; }
		}
		comps.push({ minX, minY, maxX, maxY, area }); id++;
	}
	return { label, W, H, comps: comps.filter((c) => c.area >= minArea) };
}
// --- 3) Separación morfológica ligera (sprites que se tocan) ----------------
// Opcional (--split). Erosión 1 px sobre la máscara de foreground y etiquetado de
// los componentes del "núcleo": separa piezas que se tocan por un borde fino.
// Llamado desde main() SOLO con --split (sustituye a extractMulti).
function splitTouching(png, bgs, tol, minArea) {
	const W = png.width, H = png.height;
	const fg = new Uint8Array(W * H);
	for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) fg[y * W + x] = isBgMulti(png, W, bgs, tol, x, y) ? 0 : 1;
	// erosión 1 px: se conserva un píxel si sus 4 vecinos también son foreground.
	const core = new Uint8Array(W * H);
	for (let y = 1; y < H - 1; y++) for (let x = 1; x < W - 1; x++) {
		if (!fg[y * W + x]) continue;
		if (fg[(y - 1) * W + x] && fg[(y + 1) * W + x] && fg[y * W + x - 1] && fg[y * W + x + 1]) core[y * W + x] = 1;
	}
	const label = new Int32Array(W * H).fill(-1);
	const stack = new Int32Array(W * H * 2);
	const comps = [];
	let id = 0;
	for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) {
		if (!core[y * W + x] || label[y * W + x] !== -1) continue;
		let sp = 0; stack[sp++] = x; stack[sp++] = y; label[y * W + x] = id;
		let minX = x, minY = y, maxX = x, maxY = y, area = 0;
		while (sp > 0) { const cy = stack[--sp], cx = stack[--sp]; area++;
			if (cx < minX) minX = cx; if (cx > maxX) maxX = cx; if (cy < minY) minY = cy; if (cy > maxY) maxY = cy;
			for (const [dx, dy] of [[1, 0], [-1, 0], [0, 1], [0, -1]]) { const nx = cx + dx, ny = cy + dy;
				if (nx < 0 || ny < 0 || nx >= W || ny >= H) continue; if (label[ny * W + nx] !== -1) continue; if (!core[ny * W + nx]) continue;
				label[ny * W + nx] = id; stack[sp++] = nx; stack[sp++] = ny; }
		}
		comps.push({ minX, minY, maxX, maxY, area }); id++;
	}
	return comps.filter((c) => c.area >= minArea);
}

// --- 4) Heurística de rejilla (agrupa frames del mismo sprite) --------------
// Agrupa sprites por tamaño igual y su disposición en rejilla (malla completa =
// animación; filas de varios = animaciones; el resto = independientes).
// Llamado desde main(): es el fallback si no hay IA o la IA no decide.
function heuristicGroups(sprites) {
	const bySize = new Map();
	sprites.forEach((s, i) => { const k = `${s.w}x${s.h}`; if (!bySize.has(k)) bySize.set(k, []); bySize.get(k).push(i); });
	const groups = [];
	const used = new Set();
	for (const [size, idxs] of bySize) {
		const sorted = idxs.slice().sort((a, b) => sprites[a].y - sprites[b].y || sprites[a].x - sprites[b].x);
		const rows = new Map(); const cols = new Map();
		for (const i of sorted) { const r = sprites[i].y; if (!rows.has(r)) rows.set(r, []); rows.get(r).push(i); const c = sprites[i].x; if (!cols.has(c)) cols.set(c, []); cols.get(c).push(i); }
		const isLattice = rows.size >= 2 && cols.size >= 2 && rows.size * cols.size === idxs.length;
		if (isLattice) {
			// malla completa: una animación (orden por fila-columna)
			const order = [];
			for (const [, v] of rows) for (const i of v) order.push(i);
			groups.push({ name: `secuencia_${size}`, kind: 'animacion', sprites: order });
			order.forEach((i) => used.add(i));
			continue;
		}
		if (rows.size && [...rows.values()].every((v) => v.length > 1)) {
			// filas: cada fila = animación del mismo sprite
			for (const [, v] of rows) { groups.push({ name: `secuencia_${size}_${(v[0] / sprites.length).toFixed(2)}`, kind: 'animacion', sprites: v.slice() }); v.forEach((i) => used.add(i)); }
			continue;
		}
		// resto: independientes (cada pieza es su propio asset)
		for (const i of sorted) if (!used.has(i)) groups.push({ name: `independiente_${i}`, kind: 'independiente', sprites: [i] });
	}
	return groups;
}

// --- montaje transparente por grupo (para --quantize) ------------------------
// Pone los sprites del grupo en una sola hoja sobre fondo transparente, del tamaño
// de la celda más grande. Llamado desde main() en el paso --quantize N.
function makeGroupSheet(sprites, cell) {
	const cols = Math.ceil(Math.sqrt(sprites.length));
	const rows = Math.ceil(sprites.length / cols);
	const cw = cols * cell, ch = rows * cell;
	const out = new PNG({ width: cw, height: ch });
	for (let i = 0; i < cw * ch * 4; i++) out.data[i] = 0; // transparente
	sprites.forEach((s, i) => {
		const ox = (i % cols) * cell + Math.floor((cell - s.width) / 2);
		const oy = Math.floor(i / cols) * cell + Math.floor((cell - s.height) / 2);
		for (let y = 0; y < s.height; y++) for (let x = 0; x < s.width; x++) {
			const so = (y * s.width + x) * 4;
			if (s.alpha[y * s.width + x]) { const o = ((oy + y) * cw + (ox + x)) * 4; out.data[o] = s.rgb[y * s.width + x][0]; out.data[o + 1] = s.rgb[y * s.width + x][1]; out.data[o + 2] = s.rgb[y * s.width + x][2]; out.data[o + 3] = 255; }
		}
	});
	return out;
}

// spriteToPng: convierte un sprite {w,h,rgb,alpha} a un PNG pngjs. Llamado desde
// contactSheet(...) en main() y para colorear la hoja del grupo.
function spriteToPng(s) {
	const p = new PNG({ width: s.w, height: s.h });
	for (let y = 0; y < s.h; y++) for (let x = 0; x < s.w; x++) {
		const o = (y * s.w + x) * 4, a = s.alpha[y * s.w + x];
		p.data[o] = s.rgb[y * s.w + x][0]; p.data[o + 1] = s.rgb[y * s.w + x][1]; p.data[o + 2] = s.rgb[y * s.w + x][2]; p.data[o + 3] = a ? 255 : 0;
	}
	return p;
}

// main(): orquesta el pipeline completo (ver cabecera). Orden de llamadas:
//   loadImage -> detectBackgrounds -> extractMulti|splitTouching ->
//   sprites (construcción) -> heuristicGroups -> (IA vía ask/extractJson) ->
//   organiza grupos -> (opcional) subproceso amiga-tiles.mjs --quantize.
async function main() {
	const input = process.argv[2];
	if (!input || hasArg('--help') || hasArg('-h')) { console.log('Uso: node tools/amiga-tiles/game-assets.mjs <imagen> [--out DIR] [--tol N] [--min N] [--split] [--ai|--no-ai] [--tokens N] [--quantize N]'); return; }
	const tol = Math.max(1, parseInt(argV('--tol', '24'), 10) || 24);
	const minArea = Math.max(8, parseInt(argV('--min', '30'), 10) || 30);
	const doSplit = hasArg('--split'); // morfología ligera OPCIONAL (sprites que se tocan) — por defecto apagada: erosionar fragmenta sprites con detalles
	const quantize = parseInt(argV('--quantize', '0'), 10);
	const job = argV('--job', path.basename(input, path.extname(input)));
	const outDir = path.resolve(argV('--out', path.join(ROOT, 'out', job)));
	fs.mkdirSync(outDir, { recursive: true });

	const png = loadImage(input);
	console.log(`[game-assets] ${input} ${png.width}x${png.height} -> ${outDir}`);
	const bg = detectBackgrounds(png, tol);
	console.log(`[game-assets] fondos: ${bg.mode === 'alpha' ? 'alpha' : bg.bgs.length ? bg.bgs.map((c) => `rgb(${c.join(',')})`).join(' ') : 'ninguno'}`);

	let comps = doSplit ? splitTouching(png, bg.bgs, tol, minArea) : extractMulti(png, bg.bgs, tol, minArea).comps;
	const sprites = comps.map((c, i) => {
		const w = c.maxX - c.minX + 1, h = c.maxY - c.minY + 1;
		const rgb = []; const alpha = new Uint8Array(w * h);
		for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
			const sx = c.minX + x, sy = c.minY + y, o = (sy * png.width + sx) * 4;
			const isB = bg.mode === 'alpha' ? png.data[o + 3] < 16 : bg.bgs.some((b) => dist2([png.data[o], png.data[o + 1], png.data[o + 2]], b) <= tol * tol);
			rgb.push([png.data[o], png.data[o + 1], png.data[o + 2]]); alpha[y * w + x] = isB ? 0 : 1;
		}
		return { idx: i, x: c.minX, y: c.minY, w, h, rgb, alpha, center: { x: Math.round((c.minX + c.maxX) / 2), y: Math.round((c.minY + c.maxY) / 2) }, anchor: { x: Math.round((c.minX + c.maxX) / 2), y: c.maxY } };
	});
	console.log(`[game-assets] piezas: ${sprites.length}`);
	fs.mkdirSync(path.join(outDir, 'sprites'), { recursive: true });
	const metas = sprites.map((s) => ({ seq: s.idx, name: `sprite_P${String(s.idx).padStart(3, '0')}_x${s.x}_y${s.y}_w${s.w}_h${s.h}.png`, x: s.x, y: s.y, w: s.w, h: s.h, center: s.center, anchor: s.anchor, area: comps[s.idx].area }));
	for (const s of sprites) {
		const p = new PNG({ width: s.w, height: s.h });
		for (let y = 0; y < s.h; y++) for (let x = 0; x < s.w; x++) { const o = (y * s.w + x) * 4, a = s.alpha[y * s.w + x], c = s.rgb[y * s.w + x]; p.data[o] = c[0]; p.data[o + 1] = c[1]; p.data[o + 2] = c[2]; p.data[o + 3] = a ? 255 : 0; }
		fs.writeFileSync(path.join(outDir, 'sprites', metas[s.idx].name), PNG.sync.write(p));
	}
	fs.writeFileSync(path.join(outDir, 'layout.json'), JSON.stringify({ image: input, size: `${png.width}x${png.height}`, background: bg, sprites: metas }, null, 2), 'utf8');
	// Hoja de contacto para el humano (sprites_sheet.png).
	fs.writeFileSync(path.join(outDir, 'sprites_sheet.png'), PNG.sync.write(contactSheet(sprites.map(spriteToPng))));

	// --- 4) Agrupación: IA determinista con fallback heurístico ---------------
	// Regla: sin --no-ai se intenta ollama (qwen3-vl). Si la IA empaqueta TODO en un
	// solo grupo (o responde sin JSON válido / no disponible), el fallback es la
	// heurística heuristicGroups (100% determinista).
	let groups = heuristicGroups(sprites);
	let aiUsed = false, aiNote = '';
	const aiForce = hasArg('--ai');
	const aiOff = hasArg('--no-ai');
	let wantAi = !aiOff && (aiForce || !hasArg('--no-ai'));
	if (wantAi) {
		try {
			const model = argV('--model', 'qwen3-vl:8b-instruct-q8_0');
			const sheet = contactSheet(sprites.map(spriteToPng));
			const prompt = `Hay ${sprites.length} sprites etiquetados 0..${sprites.length - 1} en la hoja. Devuelve SOLO JSON valid: {"groups":[{"name":"..","sprites":[indices],"order":[indices reordenados si es animacion],"anchor":{"x":0.5,"y":0.95} o null}], "issues":[{"sprite":i,"reason":".."}]} Agrupa los sprites del mismo personaje/objeto en poses o frames de animacion; anchor como fraccion (0..1) del ancho/alto relativo al punto de anclaje (pies/bottom-center); marca en issues los que parezcan incompletos.`;
			const txt = await ask(model, argV('--ollama-base', 'http://127.0.0.1:11434'), prompt, sheet);
			fs.writeFileSync(path.join(outDir, 'ai_group.json'), JSON.stringify({ raw: txt, parsed: extractJson(txt) }, null, 2), 'utf8');
			const parsed = extractJson(txt);
			if (parsed && Array.isArray(parsed.groups) && parsed.groups.length) {
				const aiGroups = parsed.groups.map((g) => ({ name: String(g.name || 'grupo').replace(/[\\/:*?"<>| ]/g, '_'), sprites: Array.isArray(g.order) && g.order.length ? g.order : (g.sprites || []), anchor: g.anchor || null, kind: 'ia' }));
				const oneGroupAll = aiGroups.length === 1 && (aiGroups[0].sprites || []).length >= sprites.length * 0.95;
				if (!oneGroupAll && aiGroups.every((g) => (g.sprites || []).length <= sprites.length)) { groups = aiGroups; aiUsed = true; }
			}
			else aiNote = 'IA respondió sin JSON válido; uso heurística.';
		} catch (e) { aiNote = `IA no disponible (${e.message}); uso heurística.`; wantAi = false; }
	}
	if (!wantAi && !aiUsed) aiNote = 'IA desactivada por flag.';
	// Normaliza offsets (coords reales) a cada grupo
	const groupsExpanded = groups.map((g) => {
		const frames = (g.sprites || []).filter((i) => sprites[i]).map((i) => sprites[i]);
		return { name: g.name, kind: g.kind || (frames.length > 1 ? 'animacion' : 'independiente'), anchor: g.anchor || { x: 0.5, y: 1 }, frames, seq: frames.map((s) => s.idx), offsets: frames.map((s) => ({ x: s.x + Math.round(s.w * (g.anchor ? g.anchor.x : 0.5)), y: s.y + s.h })) };
	});

	// --- 5) Organizar ---------------------------------------------------------
	for (const g of groupsExpanded) {
		const gd = path.join(outDir, g.name); fs.mkdirSync(gd, { recursive: true });
		const groupMeta = { name: g.name, kind: g.kind, anchor: g.anchor, frames: [] };
		let n = 0;
		for (const s of g.frames) {
			const p = new PNG({ width: s.w, height: s.h });
			for (let y = 0; y < s.h; y++) for (let x = 0; x < s.w; x++) { const o = (y * s.w + x) * 4, a = s.alpha[y * s.w + x]; p.data[o] = s.rgb[y * s.w + x][0]; p.data[o + 1] = s.rgb[y * s.w + x][1]; p.data[o + 2] = s.rgb[y * s.w + x][2]; p.data[o + 3] = a ? 255 : 0; }
			const name = `frame_${String(n).padStart(2, '0')}_x${s.x}_y${s.y}_w${s.w}_h${s.h}.png`;
			fs.writeFileSync(path.join(gd, name), PNG.sync.write(p));
			groupMeta.frames.push({ file: name, source: { x: s.x, y: s.y, w: s.w, h: s.h }, offset: { x: s.x + Math.round(s.w * (g.anchor ? g.anchor.x : 0.5)), y: s.y + s.h } });
			n++;
		}
		groupMeta.count = n;
		fs.writeFileSync(path.join(gd, 'group.json'), JSON.stringify(groupMeta, null, 2), 'utf8');
		// 6) cuantización opcional (EHB/32/16, índice 0 transparente): monta la
		// hoja del grupo y lanza amiga-tiles.mjs como subproceso (--alpha reserva 0).
		if (quantize && n) {
			const maxD = g.frames.reduce((m, s) => Math.max(m, s.w, s.h), 16);
			const cell = Math.ceil(maxD / 16) * 16;
			const sheetPng = makeGroupSheet(g.frames, cell);
			const tmp = path.join(gd, '_sheet.png'); fs.writeFileSync(tmp, PNG.sync.write(sheetPng));
			const pal = quantize === 64 ? 'ehb' : 'adaptive';
			const r = spawnSync(process.execPath, [path.join(ROOT, 'tools', 'amiga-tiles', 'amiga-tiles.mjs'), tmp, '--colors', String(quantize), '--palette', pal, '--alpha', '--tile', '16', '--out', gd], { encoding: 'utf8' });
			fs.rmSync(tmp, { force: true });
			if (r.status !== 0) console.log(`   [warn] cuantización falló en ${g.name}: ${(r.stderr || r.stdout).slice(0, 200)}`);
			else { const m = (r.stdout.match(/reconstruct_[^\s]+\.png/) || [])[0]; const palF = (r.stdout.match(/palette_[^\s]+\.json/) || [])[0]; console.log(`   [quant] ${g.name} -> ${quantize} colores | paleta por grupo: ${palF || gd + '/palette_*.json'} (compartida por sus ${n} frames)`); }
		}
		console.log(`[game-assets] grupo '${g.name}': ${n} frames (${g.kind})`);
	}
	fs.writeFileSync(path.join(outDir, 'TRANSPARENCIA.md'), [
		'# Convención de transparencia (Amiga)','',
		'- Los PNG extraídos codifican el fondo como **alfa = 0** (transparente).','',
		'- **Índice 0 de la paleta = transparente** en el Amiga (todos los bitplanes a 0; en EHB, base 0).','',
		'- Al cuantizar usar `--alpha` para reservar el slot 0 (lo hace `--quantize`).','',
		`- Fuente: ${input} · fondos: ${bg.mode === 'alpha' ? 'alpha' : (bg.bgs.length ? bg.bgs.map((c) => `rgb(${c.join(',')})`).join(' ') : 'none')} · piezas: ${sprites.length}`,
		`- Grupo IA: ${aiUsed ? 'ollama local (qwen3-vl)' : aiNote}`, ''
	].join('\n'), 'utf8');
	console.log(`[game-assets] OK -> ${outDir}${aiUsed ? ' (grupo por IA)' : (` (${aiNote})`)}`);
}

main().catch((e) => { console.error(`[game-assets] ERROR: ${e.message}`); console.error(e.stack); process.exit(1); });