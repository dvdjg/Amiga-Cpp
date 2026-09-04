#!/usr/bin/env node
// extract-sprites.mjs — extrae sprites/fondos de una ilustración o hoja de sprites
// de forma DETERMINISTA (componentes conexos sobre un color de fondo automático o
// dado) y, opcionalmente, pide a ollama local que EVALÚE y AGRUPE los resultados.
//
// Flujo:
//   1. Detecta los colores de fondo: el del MARCO (si existe) y, ADEMÁS, las
//      familias "de croma" interiores — p. ej. un rectángulo VERDE que encierra una
//      explosión con margen blanco a su alrededor (el auto elige blanco+verde).
//   2. Etiqueta componentes conexos (4-conexos) de los píxeles NO fondo.
//   3. Para cada componente: bbox (x,y,w,h) + centro + ancla inferior; recorta y
//      convierte el color de fondo a TRANSPARENTE (alfa 0).
//   3b. Si hay una CAJA DE CROMA (rectángulo sólido de color interior), los trozos
//      cuyo centro cae dentro se FUSIONAN en UN solo sprite (p. ej. la explosión
//      completa), con transparencia SOLO contra el color de la caja.
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
//   --background auto|none|R,G,B   color de fondo (auto = borde + cromas interiores)
//   --tol N       tolerancia RGB de fondo (def 24)
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

// --- 1) Color de fondo automático (borde + familias de croma interiores) ------
// detectBackground: decide los colores de fondo de una ilustración:
//   - Si el PNG ya tiene transparencia significativa → modo 'alpha'.
//   - El color dominante del MARCO (si ocupa ≥40 % de los bordes).
//   - ADEMÁS, familias INTERIORES "de croma": regiones grandes y saturadas que no
//     están en los bordes. Es el caso típico de una hoja recortada con margen
//     neutro (blanco) que encierra un rectángulo de color (verde) con frames
//     dentro: el "auto" elige el VERDE del interior en vez del blanco del marco.
// Devuelve {mode:'alpha'} | {mode:'color',color} | {mode:'colors',colors:[..]} | {mode:'none'}.
// Llamado desde main() de este fichero; la variante multi-zona de game-assets.mjs
// (detectBackgrounds) amplía la misma idea a bandas de la imagen completa.
export function detectBackground(png, tol) {
	// Si el PNG ya tiene transparencia significativa, se usa el alpha y se ignora el color.
	let alphaN = 0;
	for (let i = 3; i < png.data.length; i += 4) if (png.data[i] < 16) alphaN++;
	if (alphaN > png.width * png.height * 0.02) return { mode: 'alpha' };
	const W = png.width, H = png.height;
	const sat = (c) => Math.max(c[0], c[1], c[2]) - Math.min(c[0], c[1], c[2]);
	const hMap = new Map();
	// fams(points): familias de color (colores unidos a distancia <= tol) con recuento.
	const fams = (points) => {
		hMap.clear();
		for (const [x, y] of points) { if (x < 0 || y < 0 || x >= W || y >= H) continue; const o = (y * W + x) * 4; const k = (png.data[o] << 16) | (png.data[o + 1] << 8) | png.data[o + 2]; hMap.set(k, (hMap.get(k) || 0) + 1); }
		const out = [];
		for (const [k, n] of [...hMap.entries()].sort((a, b) => b[1] - a[1])) {
			const c = [(k >> 16) & 255, (k >> 8) & 255, k & 255];
			const ix = out.findIndex((f) => dist2(f.color, c) <= tol * tol);
			if (ix >= 0) out[ix].count += n; else out.push({ color: c, count: n });
		}
		return out.sort((a, b) => b.count - a.count);
	};
	const edge = [];
	const push = (x, y) => { if (x < 0 || y < 0 || x >= W || y >= H) return; edge.push([x, y]); };
	for (let x = 0; x < W; x++) { push(x, 0); push(x, H - 1); }
	for (let y = 0; y < H; y++) { push(0, y); push(W - 1, y); }
	const bgs = [];
	const edgeFam = fams(edge)[0];
	// Familia dominante del marco: si cubre >=40 % de los bordes es fondo.
	if (edgeFam && edgeFam.count >= edge.length * 0.4) bgs.push({ color: edgeFam.color, src: 'edge' });
	// Familias INTERIORES: histograma global; valen si (a) cubren >=6 % de la imagen,
	// (b) no repiten un color ya elegido y (c) son de croma (saturadas) o masivas.
	const allPoints = [];
	for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) allPoints.push([x, y]);
	const total = W * H;
	for (const f of fams(allPoints)) {
		if (bgs.length >= 4) break;
		const cov = f.count / total;
		if (cov < 0.06) continue;
		if (bgs.some((b) => dist2(b.color, f.color) <= tol * tol)) continue;
		if (sat(f.color) >= 60 || cov >= 0.30) bgs.push({ color: f.color, src: 'interior' }); // croma o fondo sólido
	}
	if (!bgs.length) return { mode: 'none' };
	if (bgs.length === 1) return { mode: 'color', color: bgs[0].color, bgs };
	return { mode: 'colors', colors: bgs.map((b) => b.color), bgs };
}

// --- Rejilla/caja de croma: rectángulo sólido interior -> UN sprite por caja -----
// chromaBoxes: busca en la imagen rectángulos grandes de un color de fondo CROMA
// (los elegidos como 'interior' en detectBackground, p. ej. el verde de un marco
// que encierra una explosión). Cada caja respresenta UN asset: los trozos de
// contenido separados por los huecos del color se FUSIONAN en un solo sprite, con
// transparencia SOLO contra el color de la caja.
// Devuelve [{color, x0,y0,x1,y1, cov}]. Sin cajas claras -> [].
function chromaBoxes(png, bg, tol) {
	const list = bg.bgs || [];
	const chromas = list.filter((b) => b.src === 'interior');
	if (!chromas.length) return [];
	const W = png.width, H = png.height;
	const isC = (x, y, c) => { const o = (y * W + x) * 4; return dist2([png.data[o], png.data[o + 1], png.data[o + 2]], c) <= tol * tol; };
	// segs: tramos de fila/columna con fracción de color >= th, tolerando huecos gap.
	const segs = (a, th, gap) => { const out = []; let cur = null;
		for (let i = 0; i < a.length; i++) { if (a[i] >= th) { if (!cur) cur = { s: i, e: i }; else cur.e = i; }
			else if (cur && i - cur.e <= gap) cur.e = i; else if (cur) { out.push(cur); cur = null; } }
		if (cur) out.push(cur); return out; };
	const boxes = [];
	for (const ch of chromas) {
		const colF = [], rowF = [];
		for (let x = 0; x < W; x++) { let n = 0; for (let y = 0; y < H; y++) if (isC(x, y, ch.color)) n++; colF.push(n / H); }
		for (let y = 0; y < H; y++) { let n = 0; for (let x = 0; x < W; x++) if (isC(x, y, ch.color)) n++; rowF.push(n / W); }
		for (const c of segs(colF, 0.30, 3)) for (const r of segs(rowF, 0.30, 3)) {
			let n = 0; const tot = (c.e - c.s + 1) * (r.e - r.s + 1);
			for (let y = r.s; y <= r.e; y++) for (let x = c.s; x <= c.e; x++) if (isC(x, y, ch.color)) n++;
			if (n / tot < 0.30) continue;
			// fusiona cajas solapadas del mismo color
			const hit = boxes.find((b) => b.color === ch.color && b.x0 <= c.e && c.s <= b.x1 && b.y0 <= r.e && r.s <= b.y1);
			if (hit) { hit.x0 = Math.min(hit.x0, c.s); hit.x1 = Math.max(hit.x1, c.e); hit.y0 = Math.min(hit.y0, r.s); hit.y1 = Math.max(hit.y1, r.e); }
			else boxes.push({ color: ch.color, x0: c.s, y0: r.s, x1: c.e, y1: r.e });
		}
	}
	return boxes;
}

// --- overlay de cajas de croma (para el humano y para la IA) ------------------
// drawChromaOverlay: clona el PNG y pinta cada caja detectada (contorno magenta +
// índice amarillo). Es la imagen que recibe ollama en la pasada de VISIÓN para
// "comprender el contenido" de cada rectángulo de croma y proponer otros.
export function drawChromaOverlay(png, boxes) {
	const out = new PNG({ width: png.width, height: png.height });
	png.data.copy(out.data);
	const C = [255, 0, 255]; // magenta
	const set = (x, y, c) => { if (x < 0 || y < 0 || x >= png.width || y >= png.height) return; const o = (y * png.width + x) * 4; out.data[o] = c[0]; out.data[o + 1] = c[1]; out.data[o + 2] = c[2]; out.data[o + 3] = 255; };
	boxes.forEach((b, i) => {
		for (let x = b.x0; x <= b.x1; x++) { set(x, b.y0, C); set(x, b.y1, C); }
		for (let y = b.y0; y <= b.y1; y++) { set(b.x0, y, C); set(b.x1, y, C); }
		drawLabel(out, b.x0 + 2, Math.max(0, b.y0 - 9), String(i), png.width);
	});
	return out;
}

// dominantInRect: histograma en un rectángulo y devuelve la familia de color más
// frecuente {color, cov(0..1)}. Se usa para REFINAR el croma que propone la IA.
function dominantInRect(png, rect, tol) {
	const W = png.width, H = png.height;
	const h = new Map();
	for (let y = Math.max(0, rect.y0); y <= Math.min(H - 1, rect.y1); y++) for (let x = Math.max(0, rect.x0); x <= Math.min(W - 1, rect.x1); x++) {
		const o = (y * W + x) * 4; const k = (png.data[o] << 16) | (png.data[o + 1] << 8) | png.data[o + 2]; h.set(k, (h.get(k) || 0) + 1);
	}
	const out = [];
	for (const [k, n] of [...h.entries()].sort((a, b) => b[1] - a[1])) {
		const c = [(k >> 16) & 255, (k >> 8) & 255, k & 255]; const ix = out.findIndex((f) => dist2(f.color, c) <= tol * tol);
		if (ix >= 0) out[ix].count += n; else out.push({ color: c, count: n });
	}
	out.sort((a, b) => b.count - a.count);
	const tot = (Math.min(H - 1, rect.y1) - Math.max(0, rect.y0) + 1) * (Math.min(W - 1, rect.x1) - Math.max(0, rect.x0) + 1);
	return out.length ? { color: out[0].color, cov: out[0].count / tot } : null;
}

// chromaMergeInRect: extrae en un rectángulo el contenido NO-croma como UN sprite
// fusionado (alfa 0 contra el color key), con bbox ajustado al contenido.
// Devuelve {width,height,data,bbox:{x,y,w,h}, contentN} o null si casi vacío.
// Es el motor de la FUSIÓN de cajas (explosión sobre rectángulo verde) y de las
// cajas EXTRA que propone la IA.
function chromaMergeInRect(png, key, tol, rect) {
	const W = png.width, H = png.height;
	const isK = (x, y) => dist2([png.data[(y * W + x) * 4], png.data[(y * W + x) * 4 + 1], png.data[(y * W + x) * 4 + 2]], key) <= tol * tol;
	let mnX = rect.x1, mxX = rect.x0, mnY = rect.y1, mxY = rect.y0, contentN = 0;
	for (let y = Math.max(0, rect.y0); y <= Math.min(H - 1, rect.y1); y++) for (let x = Math.max(0, rect.x0); x <= Math.min(W - 1, rect.x1); x++) {
		if (isK(x, y)) continue;
		contentN++;
		if (x < mnX) mnX = x; if (x > mxX) mxX = x; if (y < mnY) mnY = y; if (y > mxY) mxY = y;
	}
	if (mxX < mnX || contentN < 8) return null;
	const w = mxX - mnX + 1, h = mxY - mnY + 1;
	const crop = Buffer.alloc(w * h * 4, 0);
	for (let y = mnY; y <= mxY; y++) for (let x = mnX; x <= mxX; x++) {
		const o = (y * W + x) * 4, ci = (y - mnY) * w + (x - mnX);
		crop[ci * 4] = png.data[o]; crop[ci * 4 + 1] = png.data[o + 1]; crop[ci * 4 + 2] = png.data[o + 2];
		crop[ci * 4 + 3] = isK(x, y) ? 0 : 255;
	}
	return { width: w, height: h, data: crop, bbox: { x: mnX, y: mnY, w, h }, contentN };
}

// --- 2+3) Componentes conexos 4-way sobre máscara NO-fondo --------------------
export function isBgPx(png, bg, tol, x, y) {
	const o = (y * png.width + x) * 4;
	if (bg.mode === 'alpha') return png.data[o + 3] < 16;
	const c = [png.data[o], png.data[o + 1], png.data[o + 2]];
	if (bg.colors) return bg.colors.some((b) => dist2(c, b) <= tol * tol);
	if (bg.color) return dist2(c, bg.color) <= tol * tol;
	return false;
}
// extract: etiqueta componentes conexos 4-way sobre la máscara NO-fondo (flood-fill
// iterativo con pila, sin recursión). Devuelve {label, W, H, comps:[{bbox,area}]}
// con los componentes que superan minArea. Llamado desde main() de este fichero.
export function extract(png, bg, tol, minArea) {
	const W = png.width, H = png.height;
	const isBg = (x, y) => isBgPx(png, bg, tol, x, y);
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
		crop[c + 3] = isBgPx(png, bg, tol, sx, sy) ? 0 : 255;
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
	const bgList = bg.colors || (bg.color ? [bg.color] : []);
	const bgLog = bg.mode === 'alpha' ? 'alpha (ya transparente)' : bgList.length ? bgList.map((c) => `rgb(${c.join(',')})`).join(' ') : 'ninguno detectable';
	console.log(`[sprites] fondo: ${bgLog}`);

	const { label, W, H, comps } = extract(png, bg, tol, minArea);
	const chroma = chromaBoxes(png, bg, tol);
	if (chroma.length) console.log(`[sprites] caja(s) de croma: ${chroma.map((b) => `[${b.x0},${b.y0}]-[${b.x1},${b.y1}] rgb(${b.color.join(',')})`).join('  ')}`);
	console.log(`[sprites] componentes: ${comps.length}`);
	fs.mkdirSync(outDir, { recursive: true });
	const dir = path.join(outDir, 'sprites'); fs.mkdirSync(dir, { recursive: true });

	const sprites = [];
	const metas = [];
	const usedInBox = new Uint8Array(comps.length);
	// 1) Sprites de cada CAJA DE CROMA: los trozos cuyo centro cae dentro del
	//    rectángulo sólido de color se FUSIONAN en UN solo sprite (p. ej. una
	//    explosión sobre el rectángulo verde), con alfa 0 SOLO contra el color de la
	//    caja (lo blanco del fogonazo se conserva, no es fondo aquí).
	for (const box of chroma) {
		const members = [];
		comps.forEach((c, i) => {
			if (usedInBox[i]) return;
			const cx = Math.round((c.minX + c.maxX) / 2), cy = Math.round((c.minY + c.maxY) / 2);
			if (cx >= box.x0 && cx <= box.x1 && cy >= box.y0 && cy <= box.y1) { members.push(c); usedInBox[i] = 2; }
		});
		if (!members.length) continue;
		const mnX = Math.min(...members.map((c) => c.minX)), mxX = Math.max(...members.map((c) => c.maxX));
		const mnY = Math.min(...members.map((c) => c.minY)), mxY = Math.max(...members.map((c) => c.maxY));
		const w = mxX - mnX + 1, h = mxY - mnY + 1;
		const crop = Buffer.alloc(w * h * 4, 0);
		for (let y = mnY; y <= mxY; y++) for (let x = mnX; x <= mxX; x++) {
			const o = (y * W + x) * 4, ci = (y - mnY) * w + (x - mnX);
			crop[ci * 4] = png.data[o]; crop[ci * 4 + 1] = png.data[o + 1]; crop[ci * 4 + 2] = png.data[o + 2];
			crop[ci * 4 + 3] = dist2([png.data[o], png.data[o + 1], png.data[o + 2]], box.color) <= tol * tol ? 0 : 255;
		}
		const s = { width: w, height: h, data: crop };
		const seq = metas.length;
		const name = `sprite_P${String(seq).padStart(3, '0')}_x${mnX}_y${mnY}_w${w}_h${h}.png`;
		fs.writeFileSync(path.join(dir, name), PNG.sync.write(s));
		sprites.push(s);
		metas.push({ seq, x: mnX, y: mnY, w, h, name, area: members.reduce((a, c) => a + c.area, 0), box: { x0: box.x0, y0: box.y0, x1: box.x1, y1: box.y1, color: box.color }, merged: true, center: { x: Math.round((mnX + mxX) / 2), y: Math.round((mnY + mxY) / 2) }, anchor: { x: Math.round((mnX + mxX) / 2), y: mxY } });
	}
	// 2) Resto de componentes (sobre el fondo "tema", p. ej. el blanco): individuales.
	comps.forEach((c, i) => {
		if (usedInBox[i]) return;
		const s = cropSprite(png, label, W, H, c, tol, bg);
		const seq = metas.length;
		const name = `sprite_P${String(seq).padStart(3, '0')}_x${c.minX}_y${c.minY}_w${s.width}_h${s.height}.png`;
		fs.writeFileSync(path.join(dir, name), PNG.sync.write(s));
		sprites.push(s);
		metas.push({ seq, x: c.minX, y: c.minY, w: s.width, h: s.height, name, area: c.area, center: { x: Math.round((c.minX + c.maxX) / 2), y: Math.round((c.minY + c.maxY) / 2) }, anchor: { x: Math.round((c.minX + c.maxX) / 2), y: c.maxY } });
	});
	fs.writeFileSync(path.join(outDir, 'sprites.json'), JSON.stringify({ image: input, size: `${W}x${H}`, background: bg, chromaBoxes: chroma, sprites: metas }, null, 2), 'utf8');
	// Overlay de depuración (cajas de croma marcadas) — utilidad y entrada a la IA.
	if (chroma.length) fs.writeFileSync(path.join(outDir, 'chroma_boxes.png'), PNG.sync.write(drawChromaOverlay(png, chroma)));

	// Hoja de contacto a tamaño natural (para el humano) + etiquetada (para la IA)
	const sheet = contactSheet(sprites, false);
	fs.writeFileSync(path.join(outDir, 'sprites_sheet.png'), PNG.sync.write(sheet));
	console.log(`[sprites] ${sprites.length} sprites -> ${dir} (sheet: sprites_sheet.png)`);
	for (const m of metas) console.log(`   #${m.seq} ${m.name}  centro(${m.center.x},${m.center.y}) ancla(${m.anchor.x},${m.anchor.y})`);

	// --- IA: 1) COMPRENDER las cajas de croma + proponer cromas extra ----------
	// Pasada de VISIÓN: ollama recibe el overlay con las cajas marcadas y devuelve
	// (a) qué CONTIENE cada caja, (b) OTROS rectángulos de color sólido que vio como
	// fondo croma y que el determinismo no marcó. Los que propaga se extraen al
	// instante con chromaMergeInRect (color refinado con dominantInRect).
	if (aiMode) {
		const model = argV('--model', 'qwen3-vl:8b-instruct-q8_0');
		let vision = null;
		try {
			const pVision = `Analiza esta hoja de un videojuego. Los rectángulos MAGENTA marcados (índices ${chroma.length ? '0..' + (chroma.length - 1) : 'ninguno'}) son "cajas de croma": fondos de color sólido sobre los que se dibujó contenido (sprites). Devuelve SOLO JSON:
{"boxes":[{"index":0,"content":"descripción corta del contenido","type":"explosion|personaje|vehiculo|fondo|otro","frames":N o null}],"extraChroma":[{"x0pct":..,"y0pct":..,"x1pct":..,"y1pct":..,"color":[r,g,b],"content":"descripción"}]}
Para cada caja marcada (según su índice) dime brevemente qué contiene. Si ves OTRO rectángulo GRANDE de color sólido (verde, magenta, azul, negro...) que no está marcado y que se usa como fondo para dibujar otra entidad, propónlo en extraChroma con su rectángulo en % del ancho y alto de la IMAGEN COMPLETA (0..100) y su color aproximado [r,g,b]. No inventes cajas si no hay.`;
			const ov = drawChromaOverlay(png, chroma);
			const t1 = await ask(model, base, pVision, ov);
			vision = extractJson(t1);
			fs.writeFileSync(path.join(outDir, 'vision_boxes.json'), JSON.stringify({ raw: t1, parsed: vision }, null, 2), 'utf8');
			console.log(`[sprites] IA cajas: ${vision && Array.isArray(vision.boxes) ? `${vision.boxes.length} comprendidas` : 'sin JSON'}${vision && Array.isArray(vision.extraChroma) && vision.extraChroma.length ? ` + ${vision.extraChroma.length} cromas extra` : ''} -> vision_boxes.json`);
		} catch (e) { console.error(`[sprites] ollama (cajas) falló: ${e.message}`); }

		// 1b) aplicar cajas EXTRA propuestas por la IA (crop + croma + sprite fusionado).
		const clampI = (v, lo, hi) => Math.max(lo, Math.min(hi, Math.round(v)));
		if (vision && Array.isArray(vision.extraChroma)) {
			for (const ex of vision.extraChroma.slice(0, 6)) {
				const rect = { x0: clampI((+ex.x0pct || 0) / 100 * W, 0, W - 1), y0: clampI((+ex.y0pct || 0) / 100 * H, 0, H - 1), x1: clampI((+ex.x1pct || 0) / 100 * W, 0, W - 1), y1: clampI((+ex.y1pct || 0) / 100 * H, 0, H - 1) };
				if (rect.x1 <= rect.x0 || rect.y1 <= rect.y0) continue;
				let key = Array.isArray(ex.color) && ex.color.length === 3 ? ex.color.map((v) => clampI(+v, 0, 255)) : null;
				const dom = dominantInRect(png, rect, tol);
				if (dom && dom.cov >= 0.05) key = dom.color;            // refinamiento determinista
				if (!key) continue;
				const m = chromaMergeInRect(png, key, tol, rect);
				if (!m) { console.log(`   [visión] caja extra descartada (casi vacía): ${JSON.stringify(rect)}`); continue; }
				// Anti-duplicado: si el contenido ya lo cubre una caja fusionada previa.
				const newR = { x: m.bbox.x, y: m.bbox.y, w: m.bbox.w, h: m.bbox.h };
				const dup = metas.find((mm) => mm.merged && !mm.aiProposed && mm.x <= newR.x + newR.w && newR.x <= mm.x + mm.w && mm.y <= newR.y + newR.h && newR.y <= mm.y + mm.h);
				if (dup) { console.log(`   [visión] caja extra solapada con sprite #${dup.seq}: ${JSON.stringify(newR)} (se ignora)`); continue; }
				const seq = metas.length;
				const name = `sprite_P${String(seq).padStart(3, '0')}_x${m.bbox.x}_y${m.bbox.y}_w${m.bbox.w}_h${m.bbox.h}.png`;
				fs.writeFileSync(path.join(dir, name), PNG.sync.write(m));
				sprites.push(m);
				const content = String(ex.content || 'croma_extra').replace(/[\\/:*?"<>| ]/g, '_');
				metas.push({ seq, x: m.bbox.x, y: m.bbox.y, w: m.bbox.w, h: m.bbox.h, name, area: m.contentN, box: rect, merged: true, aiProposed: true, content, anchor: { x: 0.5, y: 1 }, center: { x: Math.round((m.bbox.x + m.bbox.x + m.bbox.w) / 2), y: Math.round((m.bbox.y + m.bbox.y + m.bbox.h) / 2) } });
				console.log(`   [visión] croma extra ok: ${name} (${content}, key rgb(${key.join(',')}))`);
				if (organize) {
					const gd = path.join(outDir, content); fs.mkdirSync(gd, { recursive: true });
					fs.copyFileSync(path.join(dir, name), path.join(gd, `frame_00_${name}`));
					fs.writeFileSync(path.join(gd, 'group.json'), JSON.stringify({ name: content, kind: 'independiente', anchor: { x: 0.5, y: 1 }, frames: [{ file: `frame_00_${name}`, offset: { x: m.bbox.x, y: m.bbox.y + m.bbox.h } }], count: 1 }, null, 2), 'utf8');
				}
			}
		}
		// 1c) organizar por CONTENIDO las cajas que la IA comprendió.
		if (organize && vision && Array.isArray(vision.boxes)) {
			for (const b of vision.boxes) {
				const bx = chroma[b.index];
				if (!bx || !b.content) continue;
				const mt = metas.find((mm) => mm.merged && mm.box && mm.box.x0 === bx.x0 && mm.box.y0 === bx.y0 && mm.box.x1 === bx.x1 && mm.box.y1 === bx.y1);
				if (!mt) continue;
				const content = String(b.content).replace(/[\\/:*?"<>| ]/g, '_');
				const gd = path.join(outDir, content); fs.mkdirSync(gd, { recursive: true });
				fs.copyFileSync(path.join(dir, mt.name), path.join(gd, `frame_00_${mt.name}`));
				fs.writeFileSync(path.join(gd, 'group.json'), JSON.stringify({ name: b.content, kind: b.type || 'otro', anchor: { x: 0.5, y: 1 }, frames: b.frames || (mt ? 1 : 0), files: [mt ? `frame_00_${mt.name}` : ''].filter(Boolean), count: 1 }, null, 2), 'utf8');
				console.log(`   [visión] caja #${b.index} -> grupo '${content}' (${b.type || 'otro'})`);
			}
		}
	}

	// --- IA: 2) revisar + agrupar (hoja etiquetada completa: N sprites) ---------
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