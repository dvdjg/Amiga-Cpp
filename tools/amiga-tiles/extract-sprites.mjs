#!/usr/bin/env node
// extract-sprites.mjs — extrae sprites/fondos de una ilustración o hoja de sprites
// de forma DETERMINISTA (componentes conexos sobre un color de fondo automático o
// dado) y, opcionalmente, pide a ollama local que EVALÚE y AGRUPE los resultados.
//
// Flujo:
//   1. Detecta el color de fondo (bordes) o usa el alpha si el PNG ya es transparente.
//   2. Etiqueta componentes conexos (4-conexos) de los píxeles NO fondo.
//   3. Para cada componente: bbox (x,y,w,h) + centro + ancla inferior; recorta y
//      convierte el color de fondo a TRANSPARENTE (alfa 0).
//   4. Guarda cada sprite como
//      sprite_P<seq>_x<X>_y<Y>_w<W>_h<H>.png (+ sprites.json con metadatos).
//   5. (opcional --ai) Monta las piezas en una hoja con etiquetas y pide a ollama
//      local un JSON: qué piezas son entidades completas, agrupaciones por identidad
//      animada (mismo personaje/objeto en poses/frames) y, por grupo, el offset de
//      anclaje para alinear los frames.
//   6. (opcional --organize) Copia cada pieza a <out>/<grupo>/frame_<n>_<nombre>.png
//      + <out>/<grupo>/group.json (secuencia y offset).
//
// Uso:
//   node tools/amiga-tiles/extract-sprites.mjs <imagen.png|jpg> [opciones]
//   --background auto|none|R,G,B   color de fondo (auto = dominante en bordes)
//   --tol N       tolerancia RGB del color de fondo (def 24)
//   --min N       ignora componentes menores que N px de área (def 24)
//   --out DIR     salida (def: junto a la imagen en sprites/ o ./out)
//   --ai [model]  pedir a ollama local evalúe/agrupe (hoja etiquetada, 1 llamada)
//   --ai-strict   además revisar pieza a pieza (una llamada por pieza, hasta N)
//   --organize    volcar en carpetas por grupo (requiere --ai) con group.json
//   --ollama-base URL   (def 127.0.0.1:11434)  --tokens N  --limit N
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { PNG } from 'pngjs';
import jpeg from 'jpeg-js';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(__dirname, '../..');
const argV = (n, d) => { const i = process.argv.indexOf(n); return i >= 0 && i + 1 < process.argv.length ? process.argv[i + 1] : d; };
const hasArg = (n) => process.argv.indexOf(n) >= 0;

// loadImage: lee PNG (pngjs) o JPEG (jpeg-js) a RGBA uniforme {width,height,data}.
// Llamado desde main() de este fichero y desde game-assets.mjs (vía import).
export function loadImage(p) {
	const buf = fs.readFileSync(p);
	if (/\.jpe?g$/i.test(p)) { const d = jpeg.decode(buf, { useTArray: true, formatAsRGBA: true }); return { width: d.width, height: d.height, data: Buffer.from(d.data) }; }
	return PNG.sync.read(buf);
}
const dist2 = (a, b) => { const dr = a[0] - b[0], dg = a[1] - b[1], db = a[2] - b[2]; return dr * dr + dg * dg + db * db; };

// --- 1) Color de fondo automático (dominante en los bordes) ------------------
// detectBackground: decide el color de fondo de una ilustración (o alpha si el PNG
// ya es transparente). Vota el color más frecuente del MARCO de la imagen.
// Llamado desde main() de este fichero; es la base para la variante multi-zona de
// game-assets.mjs (detectBackgrounds).
export function detectBackground(png, tol) {
	// Si el PNG ya tiene transparencia significativa, se usa el alpha y se ignora el color.
	let alphaN = 0;
	for (let i = 3; i < png.data.length; i += 4) if (png.data[i] < 16) alphaN++;
	if (alphaN > png.width * png.height * 0.02) return { mode: 'alpha' };
	// Muestreo de bordes (1..2 px) y voto del color más frecuente.
	const W = png.width, H = png.height;
	const counts = new Map();
	const edge = [];
	const push = (x, y) => { if (x < 0 || y < 0 || x >= W || y >= H) return; edge.push([x, y]); };
	for (let x = 0; x < W; x++) { push(x, 0); push(x, H - 1); }
	for (let y = 0; y < H; y++) { push(0, y); push(W - 1, y); }
	for (const [x, y] of edge) { const o = (y * W + x) * 4; const k = (png.data[o] << 16) | (png.data[o + 1] << 8) | png.data[o + 2]; counts.set(k, (counts.get(k) || 0) + 1); }
	// Agrupar por tolerancia: cuenta "familia" del color más común.
	const sorted = [...counts.entries()].sort((a, b) => b[1] - a[1]);
	const [topK, topN] = sorted[0];
	const family = sorted.filter(([k]) => { const c = [(k >> 16) & 255, (k >> 8) & 255, k & 255], t = [(topK >> 16) & 255, (topK >> 8) & 255, topK & 255]; return dist2(c, t) <= tol * tol; });
	const famN = family.reduce((s, e) => s + e[1], 0);
	if (famN >= edge.length * 0.4) return { mode: 'color', color: [(topK >> 16) & 255, (topK >> 8) & 255, topK & 255] };
	return { mode: 'none' };
}

// --- 2+3) Componentes conexos 4-way sobre máscara NO-fondo --------------------
// extract: etiqueta componentes conexos 4-way sobre la máscara NO-fondo (flood-fill
// iterativo con pila, sin recursión). Devuelve {label, W, H, comps:[{bbox,area}]}
// con los componentes que superan minArea. Llamado desde main() de este fichero.
export function extract(png, bg, tol, minArea) {
	const W = png.width, H = png.height;
	const isBg = (x, y) => {
		const o = (y * W + x) * 4;
		if (bg.mode === 'alpha') return png.data[o + 3] < 16;
		if (bg.mode === 'color') return dist2([png.data[o], png.data[o + 1], png.data[o + 2]], bg.color) <= tol * tol;
		return false;
	};
	const label = new Int32Array(W * H).fill(-1);
	const comps = []; // {bbox}
	const stack = new Int32Array(W * H * 2);
	const visit = (x, y, id) => {
		let sp = 0;
		stack[sp++] = x; stack[sp++] = y;
		label[y * W + x] = id;
		let minX = x, minY = y, maxX = x, maxY = y, area = 0;
		while (sp > 0) {
			const cy = stack[--sp], cx = stack[--sp]; // se empuja (x,y): se desapila y luego x
			area++;
			if (cx < minX) minX = cx; if (cx > maxX) maxX = cx;
			if (cy < minY) minY = cy; if (cy > maxY) maxY = cy;
			for (const [dx, dy] of [[1, 0], [-1, 0], [0, 1], [0, -1]]) {
				const nx = cx + dx, ny = cy + dy;
				if (nx < 0 || ny < 0 || nx >= W || ny >= H) continue;
				if (label[ny * W + nx] !== -1) continue;
				if (isBg(nx, ny)) continue;
				label[ny * W + nx] = id;
				stack[sp++] = nx; stack[sp++] = ny;
			}
		}
		comps[id] = { minX, minY, maxX, maxY, area };
	};
	let id = 0;
	for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) {
		if (label[y * W + x] !== -1) continue;
		if (isBg(x, y)) { label[y * W + x] = -2; continue; }
		visit(x, y, id++);
	}
	return { label, W, H, comps: comps.filter((c) => c.area > 0 && c.area >= minArea) };
}

// --- Salida: recorta cada componente a RGBA con fondo transparente -------------
// cropSprite: recorta un componente a su bbox y convierte el fondo a TRANSPARENTE
// (alfa 0; convenio Amiga: índice 0 = transparente). Llamado desde main() de este
// fichero (y era la base del pipeline hoy unificado en game-assets.mjs).
export function cropSprite(png, label, W, H, comp, tol, bg) {
	const w = comp.maxX - comp.minX + 1, h = comp.maxY - comp.minY + 1;
	const crop = Buffer.alloc(w * h * 4, 0);
	for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
		const sx = comp.minX + x, sy = comp.minY + y;
		const o = (sy * W + sx) * 4;
		const c = (y * w + x) * 4;
		crop[c] = png.data[o]; crop[c + 1] = png.data[o + 1]; crop[c + 2] = png.data[o + 2];
		const isBgPx = bg.mode === 'alpha' ? png.data[o + 3] < 16
			: bg.mode === 'color' ? dist2([png.data[o], png.data[o + 1], png.data[o + 2]], bg.color) <= tol * tol : false;
		crop[c + 3] = isBgPx ? 0 : 255;
	}
	return { width: w, height: h, data: crop };
}

// --- hoja de contacto ETIQUETADA (para la IA) con mini-fuente 5x7 -------------
const F5 = {
	'0': [0x0e,0x11,0x13,0x15,0x19,0x11,0x0e], '1': [0x04,0x0c,0x04,0x04,0x04,0x04,0x0e], '2': [0x0e,0x11,0x01,0x02,0x04,0x08,0x1f],
	'3': [0x1e,0x01,0x01,0x0e,0x01,0x01,0x1e], '4': [0x02,0x06,0x0a,0x12,0x1f,0x02,0x02], '5': [0x1f,0x10,0x10,0x1e,0x01,0x01,0x1e],
	'6': [0x0e,0x10,0x10,0x1e,0x11,0x11,0x0e], '7': [0x1f,0x01,0x02,0x04,0x08,0x08,0x08], '8': [0x0e,0x11,0x11,0x0e,0x11,0x11,0x0e], '9': [0x0e,0x11,0x11,0x0f,0x01,0x01,0x0e],
};
function drawLabel(out, ox, oy, text, W) {
	for (let i = 0; i < text.length; i++) {
		const g = F5[text[i]]; if (!g) continue;
		for (let r = 0; r < 7; r++) for (let c = 0; c < 5; c++) {
			if (!(g[r] & (1 << (4 - c)))) continue;
			const x = ox + i * 6 + c, y = oy + r;
			if (x >= 0 && y >= 0 && x < W && y < 60) { const o = (y * W + x) * 4; out.data[o] = 255; out.data[o + 1] = 255; out.data[o + 2] = 96; out.data[o + 3] = 255; }
		}
	}
}

// contactSheet: monta los sprites en una hoja de contacto con etiqueta numérica
// (mini-fuente 5x7) para que la IA pueda referirse a cada pieza por índice.
// Llamado desde este fichero, desde game-assets.mjs y desde run-demos.mjs.
export function contactSheet(sprites, append) {
	const cell = 96, cols = Math.max(1, Math.min(10, sprites.length));
	const rows = Math.ceil(sprites.length / cols);
	const cw = cols * cell, ch = 40 + rows * cell;
	const out = new PNG({ width: cw, height: ch });
	for (let i = 0; i < cw * ch * 4; i++) out.data[i] = 0;
	sprites.forEach((s, i) => {
		const cx = (i % cols) * cell, cy = 40 + Math.floor(i / cols) * cell;
		const sx = Math.min(s.width - 1, Math.max(0, Math.floor((cell - 8) / 2 - s.width / 2)));
		const sy = Math.min(s.height - 1, Math.max(0, Math.floor((cell - 8) / 2 - s.height / 2)));
		drawLabel(out, cx + 2, 2, String(i), cw);
		for (let y = 0; y < s.height; y++) for (let x = 0; x < s.width; x++) {
			const o = ((y + cy + 4) * cw + (x + cx + 4)) * 4, so = (y * s.width + x) * 4;
			out.data[o] = s.data[so]; out.data[o + 1] = s.data[so + 1]; out.data[o + 2] = s.data[so + 2]; out.data[o + 3] = s.data[so + 3];
		}
	});
	return out;
}

// --- ollama local ----------------------------------------------------------
// ask: una llamada a ollama local (API /v1/chat/completions) con imagen + prompt;
// max_tokens por defecto 5000 (los JSON de agrupación son largos).
// Llamado desde main() de este fichero y desde game-assets.mjs (--ai).
export async function ask(model, base, prompt, png) {
	const b64 = PNG.sync.write(png).toString('base64');
	// Tokens por defecto MUY altos (5000): los JSON de agrupación con decenas de
	// piezas son largos y antes se truncaban; ajustable con --tokens N.
	const body = { model, temperature: 0.1, max_tokens: parseInt(argV('--tokens', '5000'), 10),
		messages: [{ role: 'user', content: [
			{ type: 'text', text: prompt },
			{ type: 'image_url', image_url: { url: `data:image/png;base64,${b64}` } },
		] }] };
	const r = await fetch(`${base}/v1/chat/completions`, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) });
	if (!r.ok) throw new Error(`ollama ${r.status}: ${await r.text()}`);
	const j = await r.json();
	return (j.choices?.[0]?.message?.content ?? '');
}
// extractJson: extrae el primer objeto {...} de la respuesta de la IA (tolera
// texto envolvente). Devuelve null si no hay JSON parseable.
// Llamado desde main() de este fichero y desde game-assets.mjs (--ai).
export function extractJson(text) {
	const m = text.match(/\{[\s\S]*\}/);
	if (!m) return null;
	try { return JSON.parse(m[0]); } catch { return null; }
}

async function main() {
	const input = process.argv[2];
	if (!input || hasArg('--help') || hasArg('-h')) {
		console.log('Uso: node tools/amiga-tiles/extract-sprites.mjs <imagen> [--background auto|none|R,G,B] [--tol N] [--min N] [--ai] [--organize] [--out DIR]');
		return;
	}
	const outDir = path.resolve(argV('--out', path.join(path.dirname(path.resolve(input)), 'sprites_out')));
	const tol = Math.max(1, parseInt(argV('--tol', '24'), 10) || 24);
	const minArea = Math.max(4, parseInt(argV('--min', '24'), 10) || 24);
	const bgSpec = argV('--background', 'auto').toLowerCase();
	const aiMode = hasArg('--ai');
	const organize = hasArg('--organize');
	const base = argV('--ollama-base', 'http://127.0.0.1:11434');

	const png = loadImage(input);
	console.log(`[sprites] ${input} ${png.width}x${png.height}`);

	// Fondo
	let bg;
	if (bgSpec === 'none') bg = { mode: 'none' };
	else if (bgSpec !== 'auto') { const g = bgSpec.split(',').map(Number); bg = { mode: 'color', color: [g[0], g[1], g[2]] }; }
	else bg = detectBackground(png, tol);
	console.log(`[sprites] fondo: ${bg.mode === 'alpha' ? 'alpha (ya transparente)' : bg.mode === 'color' ? `rgb(${bg.color.join(',')})` : 'ninguno detectable'}`);

	const { label, W, H, comps } = extract(png, bg, tol, minArea);
	console.log(`[sprites] componentes: ${comps.length}`);
	fs.mkdirSync(outDir, { recursive: true });
	const dir = path.join(outDir, 'sprites'); fs.mkdirSync(dir, { recursive: true });

	const sprites = [];
	const metas = [];
	comps.forEach((c, seq) => {
		const s = cropSprite(png, label, W, H, c, tol, bg);
		const name = `sprite_P${String(seq).padStart(3, '0')}_x${c.minX}_y${c.minY}_w${s.width}_h${s.height}.png`;
		fs.writeFileSync(path.join(dir, name), PNG.sync.write(s));
		sprites.push(s);
		const origin = { x: c.minX, y: c.minY, w: s.width, h: s.height };
		metas.push({ seq, ...origin, name, area: c.area, center: { x: Math.round((c.minX + c.maxX) / 2), y: Math.round((c.minY + c.maxY) / 2) }, anchor: { x: Math.round((c.minX + c.maxX) / 2), y: c.maxY } });
	});
	fs.writeFileSync(path.join(outDir, 'sprites.json'), JSON.stringify({ image: input, size: `${W}x${H}`, background: bg, sprites: metas }, null, 2), 'utf8');

	// Hoja de contacto a tamaño natural (para el humano) + etiquetada (para la IA)
	const sheet = contactSheet(sprites, false);
	fs.writeFileSync(path.join(outDir, 'sprites_sheet.png'), PNG.sync.write(sheet));
	console.log(`[sprites] ${sprites.length} sprites -> ${dir} (sheet: sprites_sheet.png)`);
	for (const m of metas) console.log(`   #${m.seq} ${m.name}  centro(${m.center.x},${m.center.y}) ancla(${m.anchor.x},${m.anchor.y})`);

	// --- IA: revisar + agrupar (una llamada con la hoja etiquetada) ----------
	if (aiMode) {
		const model = argV('--model', 'qwen3-vl:8b-instruct-q8_0');
		const prompt = `Hay ${sprites.length} imágenes de sprites extraídas de un videojuego, etiquetadas del 0 al ${sprites.length - 1} en la hoja (los píxeles brillantes cercanos a cada recuadro son su número). Devuelve SOLO JSON con:
{"groups":[{"name":"p. ej. personaje_basico","sprites":[indices],"order":[indices reordenados por secuencia de animacion, si aplica],"anchor":{"x":0,"y":0} o null}], "issues":[{"sprite":i,"reason":"p. ej. incompleto/tapado por otra entidad"}]}
Agrupa los sprites que representen el MISMO objeto/personaje en distintas poses o frames. anchor en % del propio sprite (0..1) relativo al anclo de animacion (p. ej. pies/bottom-center). Si un sprite parece incompleto, ponlo en issues. Si hay un fondo grande continuo (no un sprite), incluyelo como group "fondo".`;
		const sheetAI = contactSheet(sprites, false);
		let txt = '';
		try { txt = await ask(model, base, prompt, sheetAI); } catch (e) { console.error(`[sprites] ollama falló: ${e.message}`); }
		const res = extractJson(txt);
		fs.writeFileSync(path.join(outDir, 'ai_group.json'), JSON.stringify({ raw: txt, parsed: res }, null, 2), 'utf8');
		console.log(`[sprites] IA: ${res ? 'OK' : 'no se pudo parsear JSON'} -> ${path.join(outDir, 'ai_group.json')}`);
		if (res && Array.isArray(res.groups)) {
			for (const g of res.groups) {
				const gName = String(g.name || 'grupo').replace(/[\\/:*?"<>| ]/g, '_');
				const gSeq = Array.isArray(g.order) && g.order.length ? g.order : (g.sprites || []);
				if (organize && typeof gSeq !== 'string') {
					const gd = path.join(outDir, gName); fs.mkdirSync(gd, { recursive: true });
					const meta = { name: g.name, anchor: g.anchor || null };
					let n = 0;
					for (const idx of gSeq) if (metas[idx]) { fs.copyFileSync(path.join(dir, metas[idx].name), path.join(gd, `frame_${String(n).padStart(2, '0')}_${metas[idx].name}`)); meta.frames = meta.frames || {}; meta.frames[metas[idx].seq] = n; n++; }
					fs.writeFileSync(path.join(gd, 'group.json'), JSON.stringify(meta, null, 2), 'utf8');
					console.log(`[sprites] grupo '${g.name}': ${(typeof gSeq === 'string' ? 1 : gSeq.length)} frames (ancla ${g.anchor ? JSON.stringify(g.anchor) : 'por defecto'})`);
				}
			}
		}
		if (res && Array.isArray(res.issues)) for (const i of res.issues) console.log(`   [aviso] sprite #${i.sprite}: ${i.reason}`);
	}
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
	main().catch((e) => { console.error(`[sprites] ERROR: ${e.message}`); console.error(e.stack); process.exit(1); });
}