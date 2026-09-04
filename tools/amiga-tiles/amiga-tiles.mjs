#!/usr/bin/env node
// ---------------------------------------------------------------------------
// amiga-tiles.mjs — Tool única para convertir un bitmap (o atlas de tiles) a
// un tilebank indexado del Amiga, con cuantización flexible a 4, 8, 16, 32 o
// 64 colores EHB, con o sin transparencia, dithering opcional y varios
// criterios de paleta (adaptada, externa o fija).
//
// Aúna en un solo archivo toda la funcionalidad del antiguo conjunto
// `tools/ehb` (quantize + slice + reconstruct + export + banco X-Limited),
// generalizada a cualquier profundidad de color del Amiga (excepto HAM).
//
// Uso:
//   node tools/amiga-tiles/amiga-tiles.mjs <imagen.png> [opciones]
//
// Opciones principales:
//   --colors N            4|8|16|32|64 (64 = EHB: 32 base + half). Auto si se omite.
//   --alpha / --no-alpha  Reservar el índice 0 para transparencia (auto si el PNG la tiene).
//   --dither MODE         none|floyd|atkinson|bayer   (error diffusión / matricial)
//   --dither-strength F   Intensidad de la difusión (0..1, defecto 1).
//   --palette SRC         adaptive|mediancut|kmeans|bright|ehb|perceptual|popularity|cube|grays|<archivo.json>
//   --palette-k N         Iteraciones de k-means (defecto 12).
//   --perceptual          Cuantización perceptual: histograma ponderado por
//                         luminancia/croma + métrica de distancia perceptual (distP)
//                         en el clustering y en el remap final. Reduce el sesgo hacia
//                         píxeles oscuros insignificantes y mejora los gradientes de
//                         brillo (cielos/piel/metal). Equivale a --palette perceptual.
//   --sort TYPE           none|luminance   Orden de la paleta (defecto luminance).
//   --tile N              Tamaño de tile en píxeles (defecto 16).
//   --merge F             Fusión por similitud (fracción 0..1 de índices iguales; 1 = exacto).
//   --out DIR             Directorio de salida (defecto: junto a la imagen, en out/).
//   --xlimited            Emitir además el banco interleaved de 320 px (engine X-Limited).
//   --sheet-scale 1|2     Escala de inspección del tilebank.png.
//   --prune               Emitir además variantes '_pruned_' con la paleta reducida a los
//                          colores REALMENTE usados en la imagen (menos bits/píxel y Chip RAM).
//   --chart-compact       Gráfico de paleta sin columnas de colores no usados (n=0).
// Tutorial completo: tools/amiga-tiles/README.md
// ---------------------------------------------------------------------------
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { PNG } from 'pngjs';
import jpeg from 'jpeg-js';
import { deflateSync } from 'node:zlib';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(__dirname, '../..');

// Carga PNG o JPEG (jpeg-js). Devuelve {width,height,data:Buffer RGBA}.
function loadImage(input) {
	const buf = fs.readFileSync(input);
	const ext = path.extname(input).toLowerCase();
	if (ext === '.jpg' || ext === '.jpeg') {
		const dec = jpeg.decode(buf, { useTArray: true, formatAsRGBA: true });
		return { width: dec.width, height: dec.height, data: Buffer.from(dec.data) };
	}
	return PNG.sync.read(buf);
}

// ---------------------------------------------------------------------------
// Pequeño parseador de argumentos
// ---------------------------------------------------------------------------
function arg(name, def) {
	const i = process.argv.indexOf(name);
	return i >= 0 && i + 1 < process.argv.length ? process.argv[i + 1] : def;
}
function has(name) { return process.argv.indexOf(name) >= 0; }
function intArg(name, def) { const v = arg(name, ''); const n = parseInt(v, 10); return Number.isFinite(n) ? n : def; }
function floatArg(name, def) { const v = arg(name, ''); const n = parseFloat(v); return Number.isFinite(n) ? n : def; }

// ---------------------------------------------------------------------------
// Utilidades de color (RGB444: cada componente 4 bits tras >>4)
// ---------------------------------------------------------------------------
const clamp8 = (v) => (v < 0 ? 0 : v > 255 ? 255 : v);
const to444 = (c) => ((c[0] >> 4) << 8) | ((c[1] >> 4) << 4) | (c[2] >> 4); // 0x0RGB
const word12 = (w) => [(w >> 8) & 0x0f, (w >> 4) & 0x0f, w & 0x0f].map((v) => v << 4).map((v) => v | (v >> 4));
const lum = (c) => 0.299 * c[0] + 0.587 * c[1] + 0.114 * c[2];
const dist2 = (a, b) => {
	const dr = a[0] - b[0], dg = a[1] - b[1], db = a[2] - b[2];
	return dr * dr + dg * dg + db * db;
};
const half = (c) => [c[0] >> 1, c[1] >> 1, c[2] >> 1];

// ---------------------------------------------------------------------------
// Métrica perceptual (opt-in, --perceptual).
// La distancia Euclídea pura en RGB-888 no es perceptual: sobre-representa las
// diferencias en canales que el ojo nota menos y no pondera la luminancia. Con
// --perceptual activamos una métrica ponderada por canal (más peso al verde y a
// la luminancia, que el ojo resuelve mejor). Es una aproximación barata a un
// espacio perceptual (OKLab) sin coste de conversión ni librerías.
//   distP  = 2·ΔR² + 4·ΔG² + 3·ΔB²     (verde = 4/9, azul = 3/9, rojo = 2/9 del peso)
// ---------------------------------------------------------------------------
let PERCEPTUAL = false;
const distP = (a, b) => {
	const dr = a[0] - b[0], dg = a[1] - b[1], db = a[2] - b[2];
	return 2 * dr * dr + 4 * dg * dg + 3 * db * db;
};
// dist() = la métrica activa (perceptual o Euclídea) según la bandera del módulo.
const dist = (a, b) => PERCEPTUAL ? distP(a, b) : dist2(a, b);

// ---------------------------------------------------------------------------
// Reconocimiento de PNG: dimensión, transparencia
// ---------------------------------------------------------------------------
function analyze(png) {
	let alphaN = 0;
	for (let i = 0; i < png.width * png.height; i++) { if (png.data[i * 4 + 3] < 128) alphaN++; }
	const pct = png.width * png.height ? (alphaN * 100) / (png.width * png.height) : 0;
	return { alphaN, pct };
}
function pixel(png, x, y) { const o = (y * png.width + x) * 4; return [png.data[o], png.data[o + 1], png.data[o + 2], png.data[o + 3]]; }

// ---------------------------------------------------------------------------
// Selección de profundidad: bits = log2(colors); 64 = EHB (6 bits + half)
// ---------------------------------------------------------------------------
function modeOf(colors) {
	// Cualquier N en 2..255 como slots de paleta. bits = ceil(log2 N).
	// 64 = EHB (32 base + half). 31/15/7 son slots con uno reservado (N-1 usados).
	if (!Number.isInteger(colors) || colors < 2 || colors > 255) {
		throw new Error(`--colors debe estar en 2..255 (p. ej. 4, 8, 16, 31, 32, 64=EHB, 127); se recibió ${colors}`);
	}
	const bits = Math.ceil(Math.log2(colors));
	const ehb = colors === 64;
	return { colors, bits, ehb };
}

// ---------------------------------------------------------------------------
// Histograma (contando solo píxeles opacos; la transparencia va al slot 0)
// ---------------------------------------------------------------------------
function histogram(png, ignoreAlpha, weighted) {
	const W = png.width, H = png.height;
	const counts = new Map();
	for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) {
		const c = pixel(png, x, y);
		if (ignoreAlpha && c[3] < 128) continue;
		const k = (c[0] << 16) | (c[1] << 8) | c[2];
		let w = 1;
		// Ponderación perceptual: se reduce el sesgo hacia los oscuros casi-negros
		// (píxeles insignificantes de sombras/bordes/ruido) y se favorecen los tonos
		// medios/claros y algo saturados, que el ojo percibe mejor. La frecuencia pura
		// hace que el median-cut y el k-means dediquen demasiados centros a clusters
		// oscuros casi idénticos, dejando pocos slots para cielos/piel/metal.
		if (weighted) {
			const L = 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]; // Rec.709
			const chroma = Math.max(c[0], c[1], c[2]) - Math.min(c[0], c[1], c[2]);
			w = 0.35 + 0.55 * (L / 255) + 0.25 * (chroma / 255);
		}
		counts.set(k, (counts.get(k) || 0) + w);
	}
	return [...counts.entries()].map(([k, n]) => [k, n]).sort((a, b) => b[1] - a[1]);
}
const keyToRgb = (k) => [(k >> 16) & 255, (k >> 8) & 255, k & 255];
const totalN = (hist) => hist.reduce((s, e) => s + e[1], 0);
function fillCenters(centers, hist, K) {
	// Completa hasta K centros con los colores más frecuentes que aún no estén
	// representados; elimina centros casi-duplicados (≥30 de distancia 1).
	const ok = [];
	for (const c of centers) {
		if (ok.some((o) => dist2(o, c) < 900)) continue;
		ok.push(c);
	}
	for (const [k, n] of hist) {
		if (ok.length >= K) break;
		const c = keyToRgb(k);
		if (ok.some((o) => dist2(o, c) < 400)) continue;
		ok.push(c);
	}
	while (ok.length < K) ok.push(hist[0] ? keyToRgb(hist[0][0]) : [0, 0, 0]);
	return ok.slice(0, K);
}

// ---------------------------------------------------------------------------
// PALETAS FIJAS
// ---------------------------------------------------------------------------
function fixedCubePalette(N) {
	// Malla regular en RGB444: q^3 del volumen + grises + primarios hasta N.
	const q = Math.max(1, Math.floor(Math.cbrt(N)));
	const pal = [];
	const step = q > 1 ? 15 / (q - 1) : 15;
	for (let i = 0; i < q; i++) for (let j = 0; j < q; j++) for (let k = 0; k < q; k++) {
		pal.push([Math.round(i * step), Math.round(j * step), Math.round(k * step)]);
		if (pal.length >= N) break;
	}
	const extras = [[0, 0, 0], [15, 15, 15], [15, 0, 0], [0, 15, 0], [0, 0, 15], [15, 15, 0], [0, 15, 15], [15, 0, 15]];
	for (let i = 0; pal.length < N; i++) pal.push(extras[i % extras.length]);
	return pal.slice(0, N).map(c => c.map(v => v * 16 + (v * 16 >> 4) % 4));
}
function fixedGraysPalette(N) {
	const pal = [];
	for (let i = 0; i < N; i++) { const v = Math.round((i / Math.max(1, N - 1)) * 240 + 7); pal.push([v, v, v]); }
	return pal;
}

// ---------------------------------------------------------------------------
// PALETAS ADAPTATIVAS: median-cut, k-means, mitad brillante (EHB), popularity
// ---------------------------------------------------------------------------
function medianCut(hist, K) {
	// Algoritmo de Heckbert: corta la caja de mayor rango de canal.
	const boxes = [hist]; // cada caja es una lista [key,count]
	const countBox = (b) => b.reduce((s, e) => s + e[1], 0);
	const channel = (key, c) => (key >> ((2 - c) * 8)) & 255;
	for (let split = 0; split < K - 1; split++) {
		let which = -1, rMax = -1;
		for (let i = 0; i < boxes.length; i++) {
			const b = boxes[i]; if (b.length < 2) continue;
			const mn = [255, 255, 255], mx = [0, 0, 0];
			for (const [k] of b) for (let c = 0; c < 3; c++) { const v = channel(k, c); if (v < mn[c]) mn[c] = v; if (v > mx[c]) mx[c] = v; }
			const r = Math.max(mx[0] - mn[0], mx[1] - mn[1], mx[2] - mn[2]);
			if (r > rMax) { rMax = r; which = i; }
		}
		if (which < 0) break;
		const box = boxes[which];
		let ch = -1, span = -1;
		for (let c = 0; c < 3; c++) {
			let mn = 255, mx = 0;
			for (const e of box) { const v = channel(e[0], c); if (v < mn) mn = v; if (v > mx) mx = v; }
			if (mx - mn > span) { span = mx - mn; ch = c; }
		}
		box.sort((a, b) => channel(a[0], ch) - channel(b[0], ch));
		const halfN = countBox(box) >> 1;
		let acc = 0, cutIdx = 1;
		for (let i = 0; i < box.length && cutIdx === 1; i++) { acc += box[i][1]; if (acc >= halfN) cutIdx = i + 1; }
		// Nunca dejar una caja vacía: si el corte cae en un extremo, partimos a la mitad.
		if (cutIdx <= 0 || cutIdx >= box.length) cutIdx = Math.max(1, (box.length >> 1));
		boxes.splice(which, 1, box.slice(0, cutIdx), box.slice(cutIdx));
	}
	return boxes.map((b) => {
		const n = countBox(b); if (!n) return [0, 0, 0];
		let r = 0, g = 0, bl = 0;
		for (const [k, cnt] of b) { r += channel(k, 0) * cnt; g += channel(k, 1) * cnt; bl += channel(k, 2) * cnt; }
		return [Math.round(r / n), Math.round(g / n), Math.round(bl / n)];
	}).filter(c => c !== null);
}
function kmeans(hist, K, iters, halfAware, init) {
	// k-means sobre {color} o sobre {color, half(color)} (half-aware para EHB).
	// `init` opcional: centros de partida (NO se re-seedan).
	let centers = init ? init.slice(0, K) : fillCenters(medianCut(hist, K), hist, K);
	for (let it = 0; it < iters; it++) {
		const acc = centers.map(() => [0, 0, 0, 0]);
		let moved = 0;
		for (const [k, n] of hist) {
			const c = keyToRgb(k);
			let best = 0, bd = Infinity;
			for (let i = 0; i < K; i++) {
				const dF = dist(c, centers[i]);
				const dH = halfAware ? Math.min(dF, dist(c, half(centers[i]))) : dF;
				if (dH < bd) { bd = dH; best = i; }
			}
			const useHalf = halfAware && dist(c, half(centers[best])) < dist(c, centers[best]);
			const r = useHalf ? Math.min(255, c[0] << 1) : c[0];
			const g = useHalf ? Math.min(255, c[1] << 1) : c[1];
			const b = useHalf ? Math.min(255, c[2] << 1) : c[2];
			acc[best][0] += r * n; acc[best][1] += g * n; acc[best][2] += b * n; acc[best][3] += n;
		}
		for (let i = 0; i < K; i++) {
			if (!acc[i][3]) continue;
			const next = [Math.round(acc[i][0] / acc[i][3]), Math.round(acc[i][1] / acc[i][3]), Math.round(acc[i][2] / acc[i][3])];
			if (next[0] !== centers[i][0] || next[1] !== centers[i][1] || next[2] !== centers[i][2]) { centers[i] = next; moved++; }
		}
		if (moved === 0) break;
	}
	return centers;
}
function brightSplit(hist, N) {
	const entries = hist.map(([k, n]) => { const c = keyToRgb(k); return { c, n }; }).sort((a, b) => lum(a.c) - lum(b.c));
	const total = entries.reduce((s, e) => s + e.n, 0);
	let acc = 0, cut = entries.length;
	for (let i = entries.length - 1; i >= 0; i--) { acc += entries[i].n; if (acc >= total / 2) { cut = i; break; } }
	const bright = entries.slice(cut).map((e) => [e.c[0] << 16 | e.c[1] << 8 | e.c[2], e.n]);
	return medianCut(bright, N);
}
function popularityPalette(hist, N) { return hist.slice(0, N).map(([k]) => keyToRgb(k)); }

// EHB con maximización del uso de los HALF-BRIGHTS.
// El k-means half-aware clásico deja bases en la gama oscura cuyo half (~negro)
// casi no se usa: con 32 bases "reales" se comporta casi como una paleta de 32
// colores. Este método, tras converger, mide por cada base qué fracción de sus
// píxeles eligió el half; si es poca (half desaprovechado) EMPUJA la base hacia
// los brillos (escala >1, más fuerte cuanto más oscura) y re-hace un paso de
// k-means. Así los medios tonos pasan a cubrir las sombras y los 64 colores
// efectivos (base+half) se aprovechan de verdad.
function ehbPaletteBright(hist, K, iters) {
	const base0 = kmeans(hist, K, iters, true);           // óptimo half-aware
	const ehbMSE = (bases) => {
		let mse = 0, tot = 0;
		for (const [k, n] of hist) {
			const c = keyToRgb(k); let bd = Infinity;
			for (const b of bases) { const d = Math.min(dist(c, b), dist(c, half(b))); if (d < bd) bd = d; }
			mse += bd * n; tot += n;
		}
		return mse / tot;
	};
	let bases = base0.slice();
	const useH = new Float64Array(K), useF = new Float64Array(K);
	for (let round = 0; round < 12; round++) {
		useH.fill(0); useF.fill(0);
		for (const [k, n] of hist) {
			const c = keyToRgb(k);
			let bi = 0, bd = Infinity;
			for (let i = 0; i < K; i++) {
				const d = Math.min(dist(c, bases[i]), dist(c, half(bases[i])));
				if (d < bd) { bd = d; bi = i; }
			}
			if (dist(c, half(bases[bi])) < dist(c, bases[bi])) useH[bi] += n; else useF[bi] += n;
		}
		let moved = false;
		for (let i = 0; i < K; i++) {
			const tot = useF[i] + useH[i];
			if (!tot) continue;
			if (useH[i] / tot < 0.30) {          // half desaprovechado: subir el brillo
				const b = bases[i], l = lum(b);
				const sc = l < 120 ? 1.45 : l < 200 ? 1.30 : 1.18;
				const nb = [Math.min(255, Math.round(b[0] * sc)), Math.min(255, Math.round(b[1] * sc)), Math.min(255, Math.round(b[2] * sc))];
				if (nb[0] !== b[0] || nb[1] !== b[1] || nb[2] !== b[2]) { bases[i] = nb; moved = true; }
			}
		}
		if (!moved) break;
		bases = kmeans(hist, K, 3, true, bases);
	}
	// Seguro: nunca ofrecer una paleta PEOR que el óptimo half-aware.
	return ehbMSE(bases) <= ehbMSE(base0) ? bases : base0;
}

// ---------------------------------------------------------------------------
// Tabla efectiva de colores EN ÍNDICE FINAL
//  - sin EHB:  índice 0..N-1  = N colores (0 transparente si --alpha)
//  - con EHB:  índices 0..31 = base, 32..63 = half; 0 transparente si --alpha
// ---------------------------------------------------------------------------
function buildTable(colors, bases, alpha) {
	const ehb = colors === 64;
	const table = []; // {index, rgb, transparent}
	if (ehb) {
		// 32 bases: si hay transparencia, el base 0 (y su half 32) quedan
		// reservados: los colores usables son bases 1..31 y halves 33..63.
		const usableK = alpha ? range(1, 32) : range(0, 32);
		for (const k of usableK) table.push({ index: k, rgb: bases[k] });
		for (const k of usableK) table.push({ index: 32 + k, rgb: half(bases[k]) });
		if (alpha) table.push({ index: 0, rgb: [0, 0, 0], transparent: true });
	} else {
		const n = colors;
		const usable = alpha ? [1, ...range(2, n)] : range(0, n);
		// bases tiene baseCount = colors(-1 con alpha): encaja 1:1 en los slots reales.
		for (let i = 0; i < usable.length; i++) table.push({ index: usable[i], rgb: bases[i] });
		if (alpha) table.push({ index: 0, rgb: [0, 0, 0], transparent: true });
	}
	table.sort((a, b) => a.index - b.index);
	return table;
}
function range(a, b) { const o = []; for (let i = a; i < b; i++) o.push(i); return o; }

function tableAsPalette(table) {
	const pal = [];
	for (const e of table) pal[e.index] = e.transparent ? [0, 0, 0] : e.rgb;
	return pal;
}

// ---------------------------------------------------------------------------
// DITHERING (error diffusion / matricial)
// ---------------------------------------------------------------------------
function nearestInTable(table, c) {
	let best = table[0], bd = Infinity;
	for (const e of table) { if (e.transparent) continue; const d = dist(c, e.rgb); if (d < bd) { bd = d; best = e; } }
	return best;
}
function floydCoeffs(w, h, strength) { return { dxdy: [[1, 0, 7], [-1, 1, 3], [0, 1, 5], [1, 1, 1]], div: 16, strength }; }
function atkinsonCoeffs(w, h, strength) {
	// Atkinson REFORZADO: conserva el reparto en 6 vecinos pero con acoplamiento
	// frontal fuerte ((1,0)=30 %, (2,0)=10 %) y suma de pesos = 1. Medido: el patrón
	// clásico (6 x 1/8, casi todo hacia abajo) deja con paletas escasas y fotos un
	// dither INDISTINGUIBLE de 'none' (0 píxeles cambiados vs Floyd ~50k). Con esta
	// variante el dither es visible y la textura mantiene su carácter multi-tap.
	return { dxdy: [[1, 0, 6], [2, 0, 2], [-1, 1, 3], [0, 1, 5], [1, 1, 2], [0, 2, 2]], div: 20, strength };
}
const BAYER4 = [
	[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5],
];
function quantizeIndexed(png, table, alpha, dither, strength, opts) {
	const { deadband = 0, clamp = 0, serpentine = false } = opts || {};
	const W = png.width, H = png.height;
	const out = new Uint8Array(W * H);
	const rgb = new Float32Array(W * H * 3); // error buffer
	const nearest = (c) => { const e = nearestInTable(table, c); return e.transparent ? 0 : e.index; };
	const mode = dither === 'none' ? null : dither;
	const serpent = serpentine && (mode === 'floyd' || mode === 'atkinson');
	const clampErr = (v) => clamp > 0 ? (v < -clamp ? -clamp : v > clamp ? clamp : v) : v;
	for (let y = 0; y < H; y++) {
		// Serpentine: las filas pares van L→R y las impares R→L (se rompen los worms).
		const ltr = !serpent || (y & 1) === 0;
		const x0 = ltr ? 0 : W - 1, x1 = ltr ? W : -1, xs = ltr ? 1 : -1;
		for (let x = x0; x !== x1; x += xs) {
			const c0 = pixel(png, x, y);
			const o = y * W + x;
			if (alpha && c0[3] < 128) { out[o] = 0; continue; } // transparente: no difunde
			const eo = o * 3;
			let c = [clamp8(c0[0] + clampErr(rgb[eo])), clamp8(c0[1] + clampErr(rgb[eo + 1])), clamp8(c0[2] + clampErr(rgb[eo + 2]))];
			let idx;
			if (mode === 'bayer') {
				const th = BAYER4[y & 3][x & 3];
				idx = bayerPick(table, c, th);
				// matrícial: sin propagación de error a vecinos
			} else if (mode === null) {
				// NONE: vecino más próximo puro, SIN difusión de error.
				const e = nearestInTable(table, c);
				idx = e.transparent ? 0 : e.index;
			} else {
				const eSel = nearestInTable(table, c);
				idx = eSel.index;
				const target = eSel.rgb;
				const err = [c[0] - target[0], c[1] - target[1], c[2] - target[2]];
				// DEADBAND: si el error es mínimo (zona plana casi uniforme), NO se
				// propaga → el cielo/zonas lisas quedan con un color limpio en lugar
				// de punteado entre dos colores de brillo muy distinto (artefacto
				// clásico de error diffusion). Solo difumina donde realmente hay
				// gradiente (error grande).
				const emag = Math.sqrt(err[0] * err[0] + err[1] * err[1] + err[2] * err[2]);
				if (deadband <= 0 || emag >= deadband) {
					const diff = mode === 'floyd' ? floydCoeffs() : atkinsonCoeffs();
					for (const [dx0, dy, wgt] of diff.dxdy) {
						const dx = (ltr || !serpent) ? dx0 : -dx0; // serpentina: espejo horizontal
						const nx = x + dx, ny = y + dy;
						if (nx < 0 || nx >= W || ny < 0 || ny >= H) continue;
						if (alpha && pixel(png, nx, ny)[3] < 128) continue; // no difundir sobre transparente
						const no = (ny * W + nx) * 3;
						rgb[no] += err[0] * (wgt / diff.div) * strength;
						rgb[no + 1] += err[1] * (wgt / diff.div) * strength;
						rgb[no + 2] += err[2] * (wgt / diff.div) * strength;
					}
				}
			}
			out[o] = idx;
		}
	}
	return out;
}
function bayerPick(table, c, th) {
	// Ordenado: elige el vecino más lejano si el píxel queda bajo el umbral.
	let best = table[0], bd = Infinity;
	for (const e of table) { if (e.transparent) continue; const d = dist(c, e.rgb); if (d < bd) { bd = d; best = e; } }
	const dC = dist(c, best.rgb);
	const frac = (th + 0.5) / 16;
	if (frac < 0.5) {
		// busca el segundo más cercano para puntos por encima del umbral
		let alt = null, ad = Infinity;
		for (const e of table) { if (e.transparent || e === best) continue; const d = dist(c, e.rgb); if (d < ad) { ad = d; alt = e; } }
		if (alt && frac >= (bd / (bd + ad))) return alt.index;
	}
	return best.index;
}

// ---------------------------------------------------------------------------
// SLICE: tiles únicos, fusión opcional, mapa de índices y comparación
// ---------------------------------------------------------------------------
function sliceTiles(indices, W, H, tile) {
	const cols = W / tile, rows = H / tile;
	const bank = []; const map = new Array(cols * rows);
	const seen = new Map();
	const pix = new Array(cols * rows); // Uint8Array por celda (para merge use)
	for (let ty = 0; ty < rows; ty++) for (let tx = 0; tx < cols; tx++) {
		const p = new Uint8Array(tile * tile);
		for (let yy = 0; yy < tile; yy++) for (let xx = 0; xx < tile; xx++)
			p[yy * tile + xx] = indices[(ty * tile + yy) * W + (tx * tile + xx)];
		let key = '';
		for (let i = 0; i < p.length; i++) key += String.fromCharCode(p[i] + 32);
		if (seen.has(key)) map[ty * cols + tx] = seen.get(key);
		else { seen.set(key, bank.length); map[ty * cols + tx] = bank.length; pix[ty * cols + tx] = p; bank.push({ pix: p }); }
	}
	return { bank, map, cols, rows, pix };
}
function mergeSimilar(bank, map, cols, threshold) {
	// Fusión por fracción de índices iguales (opcional; rompe el 100%).
	let absorbed = 0;
	const reps = []; const remap = new Map();
	const eqFrac = (a, b) => { let eq = 0; for (let i = 0; i < a.length; i++) if (a[i] === b[i]) eq++; return eq / a.length; };
	for (let i = 0; i < bank.length; i++) {
		let take = -1, best = 0;
		for (let r = 0; r < reps.length; r++) { const f = eqFrac(bank[i].pix, reps[r]); if (f >= threshold && f > best) { best = f; take = r; } }
		if (take >= 0) { remap.set(i, take); absorbed++; } else { remap.set(i, reps.length); reps.push(bank[i].pix); }
	}
	const newBank = reps.map((pix) => ({ pix }));
	for (let t = 0; t < map.length; t++) map[t] = remap.get(map[t]);
	return { bank: newBank, absorbed };
}
function reconstructAndCompare(indices, W, H, bank, map, cols, tile) {
	const rows = H / tile;
	const recon = new Uint8Array(W * H);
	for (let ty = 0; ty < rows; ty++) for (let tx = 0; tx < cols; tx++) {
		const p = bank[map[ty * cols + tx]].pix;
		for (let yy = 0; yy < tile; yy++) for (let xx = 0; xx < tile; xx++)
			recon[(ty * tile + yy) * W + (tx * tile + xx)] = p[yy * tile + xx];
	}
	let eq = 0; for (let i = 0; i < indices.length; i++) if (indices[i] === recon[i]) eq++;
	return { pct: (100 * eq) / indices.length, recon };
}

// ---------------------------------------------------------------------------
// EXPORT: PNG indexado con encoder propio (PLTE+IDAT) y verificación round-trip
// ---------------------------------------------------------------------------
function crc32(buf) { let c, t = crc32.table || (crc32.table = new Int32Array(256).map((_, n) => { c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; return c; })); let crc = -1; for (let i = 0; i < buf.length; i++) crc = (crc >>> 8) ^ t[(crc ^ buf[i]) & 0xff]; return (crc ^ -1) >>> 0; }
function pngChunk(type, data) { const t = Buffer.from(type, 'ascii'), len = Buffer.alloc(4); len.writeUInt32BE(data.length, 0); const crc = Buffer.alloc(4); crc.writeUInt32BE(crc32(Buffer.concat([t, data])), 0); return Buffer.concat([len, t, data, crc]); }
function writeIndexedPng(filePath, palette, indices, w, h, meta) {
	const sig = Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]);
	const ihdr = Buffer.alloc(13); ihdr.writeUInt32BE(w, 0); ihdr.writeUInt32BE(h, 4); ihdr[8] = 8; ihdr[9] = 3;
	const plte = Buffer.alloc(palette.length * 3);
	palette.forEach((c, i) => { plte[i * 3] = c[0]; plte[i * 3 + 1] = c[1]; plte[i * 3 + 2] = c[2]; });
	const rows = [];
	for (let y = 0; y < h; y++) { const r = Buffer.alloc(w + 1); r[0] = 0; for (let x = 0; x < w; x++) r[x + 1] = indices[y * w + x]; rows.push(r); }
	const chunks = [pngChunk('IHDR', ihdr)];
	// Metadatos de la conversión como tEXt (contenido ASCII: técnica/colores/dither/res).
	if (meta) chunks.push(pngChunk('tEXt', Buffer.from(`conversion\0${meta}`, 'latin1')));
	chunks.push(pngChunk('PLTE', plte), pngChunk('IDAT', deflateSync(Buffer.concat(rows))), pngChunk('IEND', Buffer.alloc(0)));
	fs.writeFileSync(filePath, Buffer.concat([sig, ...chunks]));
}
// ---------------------------------------------------------------------------
// Gráfico de uso de la paleta ("palette chart"): dado el histograma real de
// índices de la imagen convertida, pinta una tira de rectángulos (uno por slot
// de color) y debajo un histograma de barras con la frecuencia relativa de cada
// color, más textos ASCII con el slot, el nº de píxeles y el %. Es un PNG RGBA
// de INSPECCIÓN (no un activo del engine); el slot 0 transparente se muestra
// con tablero de ajedrez. Emite también las métricas por color como tEXt.
// ---------------------------------------------------------------------------
const GLYPHS = {
	' ': [[0,0,0,0,0],[0,0,0,0,0],[0,0,0,0,0],[0,0,0,0,0],[0,0,0,0,0],[0,0,0,0,0],[0,0,0,0,0]],
	'0': [[0,1,1,1,0],[1,0,0,0,1],[1,0,0,1,1],[1,0,1,0,1],[1,1,0,0,1],[1,0,0,0,1],[0,1,1,1,0]],
	'1': [[0,0,1,0,0],[0,1,1,0,0],[0,0,1,0,0],[0,0,1,0,0],[0,0,1,0,0],[0,0,1,0,0],[0,1,1,1,0]],
	'2': [[0,1,1,1,0],[1,0,0,0,1],[0,0,0,0,1],[0,0,0,1,0],[0,0,1,0,0],[0,1,0,0,0],[1,1,1,1,1]],
	'3': [[1,1,1,1,0],[0,0,0,0,1],[0,0,0,1,0],[0,0,1,1,0],[0,0,0,0,1],[1,0,0,0,1],[0,1,1,1,0]],
	'4': [[0,0,0,1,0],[0,0,1,1,0],[0,1,0,1,0],[1,0,0,1,0],[1,1,1,1,1],[0,0,0,1,0],[0,0,0,1,0]],
	'5': [[1,1,1,1,1],[1,0,0,0,0],[1,1,1,1,0],[0,0,0,0,1],[0,0,0,0,1],[1,0,0,0,1],[0,1,1,1,0]],
	'6': [[0,0,1,1,0],[0,1,0,0,0],[1,0,0,0,0],[1,1,1,1,0],[1,0,0,0,1],[1,0,0,0,1],[0,1,1,1,0]],
	'7': [[1,1,1,1,1],[0,0,0,0,1],[0,0,0,1,0],[0,0,1,0,0],[0,1,0,0,0],[0,1,0,0,0],[0,1,0,0,0]],
	'8': [[0,1,1,1,0],[1,0,0,0,1],[1,0,0,0,1],[0,1,1,1,0],[1,0,0,0,1],[1,0,0,0,1],[0,1,1,1,0]],
	'9': [[0,1,1,1,0],[1,0,0,0,1],[1,0,0,0,1],[0,1,1,1,1],[0,0,0,0,1],[0,0,0,1,0],[0,1,1,0,0]],
	'%': [[1,1,0,0,1],[1,1,0,1,0],[0,0,0,1,0],[0,0,1,0,0],[0,1,0,0,0],[1,0,1,1,0],[1,0,1,1,0]],
	'.': [[0,0,0,0,0],[0,0,0,0,0],[0,0,0,0,0],[0,0,0,0,0],[0,0,0,0,0],[0,1,1,0,0],[0,1,1,0,0]],
	',': [[0,0,0,0,0],[0,0,0,0,0],[0,0,0,0,0],[0,0,0,0,0],[0,0,1,1,0],[0,0,1,0,0],[0,1,0,0,0]],
	'-': [[0,0,0,0,0],[0,0,0,0,0],[0,0,0,0,0],[1,1,1,1,1],[0,0,0,0,0],[0,0,0,0,0],[0,0,0,0,0]],
	'(': [[0,0,0,1,0],[0,0,1,0,0],[0,1,0,0,0],[0,1,0,0,0],[0,1,0,0,0],[0,0,1,0,0],[0,0,0,1,0]],
	')': [[0,1,0,0,0],[0,0,1,0,0],[0,0,0,1,0],[0,0,0,1,0],[0,0,0,1,0],[0,0,1,0,0],[0,1,0,0,0]],
	'x': [[0,0,0,0,0],[1,0,0,0,1],[0,1,0,1,0],[0,0,1,0,0],[0,1,0,1,0],[1,0,0,0,1],[0,0,0,0,0]],
	'=': [[0,0,0,0,0],[0,0,0,0,0],[1,1,1,1,1],[0,0,0,0,0],[1,1,1,1,1],[0,0,0,0,0],[0,0,0,0,0]],
};
const FG = [255, 255, 255, 255];
function chartDrawText(buf, wpx, x, y, text, color) {
	let cx = x;
	for (const ch of text) {
		const g = GLYPHS[ch] || GLYPHS[' '];
		for (let gy = 0; gy < 7; gy++) for (let gxg = 0; gxg < 5; gxg++) if (g[gy][gxg]) {
			const px = cx + gxg, py = y + gy;
			if (px < 0 || px >= wpx || py < 0) continue;
			const o = (py * wpx + px) * 4;
			buf.data[o] = color[0]; buf.data[o + 1] = color[1]; buf.data[o + 2] = color[2]; buf.data[o + 3] = color[3];
		}
		cx += 6;
	}
	return cx;
}
function chartRect(buf, wpx, x, y, w, h, color) {
	for (let yy = y; yy < y + h; yy++) for (let xx = x; xx < x + w; xx++) {
		if (xx < 0 || xx >= wpx || yy < 0 || yy >= buf.height) continue;
		const o = (yy * wpx + xx) * 4;
		buf.data[o] = color[0]; buf.data[o + 1] = color[1]; buf.data[o + 2] = color[2]; buf.data[o + 3] = color[3];
	}
}
function paletteChartPng(outDir, suf, label, paletteFinal, indexed, alpha, bits, ehb, colors, compact) {
	const cell = 40;
	// histograma de índices de la imagen convertida
	const counts = new Float64Array(colors); let tot = 0;
	for (let i = 0; i < indexed.length; i++) { const v = indexed[i]; if (v >= 0 && v < colors) { counts[v]++; tot++; } }
	const max = Math.max(1, ...counts);
	// en modo "compacto" se omiten las columnas de colores no usados (n=0), salvo
	// el slot 0 (que en EHB/alpha es el transparente y conviene verlo siempre).
	const shown = [];
	for (let i = 0; i < colors; i++) if (!compact || counts[i] > 0 || (alpha && i === 0)) shown.push(i);
	const Wpx = Math.max(60, shown.length * cell + 8);
	const Hpx = 220;
	const buf = new PNG({ width: Wpx, height: Hpx });
	// fondo negro con rejilla tenue
	for (let y = 0; y < Hpx; y++) for (let x = 0; x < Wpx; x++) { const o = (y * Wpx + x) * 4; buf.data[o] = 12; buf.data[o + 1] = 12; buf.data[o + 2] = 14; buf.data[o + 3] = 255; }

	const barTop = 28, barH = 110, swatchH = 22;
	const x0 = 4;
	shown.forEach((i, col) => {
		const c = paletteFinal[i];
		const cx = x0 + col * cell;
		// swatch
		if (alpha && i === 0) {
			// tablero de ajedrez para el slot transparente
			for (let yy = barTop; yy < barTop + swatchH; yy++) for (let xx = cx; xx < cx + cell; xx++) {
				const o = (yy * Wpx + xx) * 4;
				const chk = (((xx >> 2) + (yy >> 2)) & 1) === 0;
				buf.data[o] = chk ? 220 : 60; buf.data[o + 1] = chk ? 220 : 60; buf.data[o + 2] = 220; buf.data[o + 3] = 255;
			}
		} else {
			chartRect(buf, Wpx, cx, barTop, cell, swatchH, [c[0], c[1], c[2], 255]);
		}
		// barra del histograma (frecuencia relativa)
		const bh = Math.max(1, Math.round((counts[i] / max) * (barH - 4)));
		chartRect(buf, Wpx, cx + 2, barTop + swatchH + 4 + (barH - bh), cell - 4, bh, [c[0], c[1], c[2], 255]);
		// borde de la columna
		chartRect(buf, Wpx, cx, barTop, cell, 1, [90, 90, 90, 255]);
		// etiquetas: slot y recuento / %
		const pct = tot ? ((counts[i] / tot) * 100) : 0;
		const t1 = `${i} (${pct.toFixed(1)}%)`;
		const ty = barTop + swatchH + 8 + barH;
		chartDrawText(buf, Wpx, cx + 2, ty, t1, FG);
		chartDrawText(buf, Wpx, cx + 2, ty + 8, `n=${counts[i]}`, FG);
	});
	// cabecera con los metadatos
	chartDrawText(buf, Wpx, 4, 4, `${label} | ${bits} bits/px${ehb ? ' EHB' : ''}${compact ? ' (compacto)' : ''}`, FG);

	// texto de los metadatos por color (tEXt) + datos en json para lectura automática
	const perColor = paletteFinal.map((c, i) => ({ slot: i, rgb: c, count: counts[i], pct: tot ? +(counts[i] / tot * 100).toFixed(2) : 0 }));
	const meta = `paletteChart;colors=${colors};bits=${bits};ehb=${ehb};alpha=${alpha ? 1 : 0};total=${tot};hist=${perColor.map((p) => p.count).join(',')}`;
	const pathF = path.join(outDir, `palette_chart_${suf}.png`);
	fs.writeFileSync(pathF, PNG.sync.write(buf));
	// incrustamos las métricas por color (tEXt) para que sean legibles sin abrir el png
	const metaTxt = perColor.map((p) => `slot${p.slot}=rgb(${p.rgb[0]},${p.rgb[1]},${p.rgb[2]}) n=${p.count} p=${p.pct}%`).join(';');
	fs.writeFileSync(path.join(outDir, `palette_chart_${suf}.txt`), metaTxt + '\n', 'utf8');
	return { pathF, meta, perColor, counts, tot, max };
}
function verifyPng(filePath, palette, indices, w, h) {
	const d = PNG.sync.read(fs.readFileSync(filePath));
	let dif = 0;
	for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
		const p = palette[indices[y * w + x]] || palette[0] || [0, 0, 0];
		const o = (y * w + x) * 4;
		if (d.data[o] !== p[0] || d.data[o + 1] !== p[1] || d.data[o + 2] !== p[2]) dif++;
	}
	const ok = d.colorType === 3 && dif === 0;
	return { colorType: d.colorType, dif };
}

// ---------------------------------------------------------------------------
// BANCO INTERLEAVED X-LIMITED (opcional). Layout: 320 px de ancho, planelínea =
// fila*planes+plano, tile t en (t%20, t/20). Bit p del índice = plano p (EHB: bit5=half).
// ---------------------------------------------------------------------------
function emitXlimitedBank(bank, tile, colors, width) {
	const planes = Math.ceil(Math.log2(colors)); // planos para representar `colors`
	const srcBytesPerRow = width / 8;
	const blocksPerRow = width / tile;
	const blockRows = Math.ceil(bank.length / blocksPerRow);
	const height = blockRows * (tile * planes);
	const out = Buffer.alloc(srcBytesPerRow * height, 0);
	const tw = tile / 8;
	for (let t = 0; t < bank.length; t++) {
		const bx = t % blocksPerRow, by = Math.floor(t / blocksPerRow);
		const base = by * (tile * planes) * srcBytesPerRow + bx * tw;
		for (let row = 0; row < tile; row++) for (let pl = 0; pl < planes; pl++) {
			const bit = 1 << pl; let word = 0;
			for (let c = 0; c < tile; c++) {
				const v = bank[t].pix[row * tile + c];
				if (v & bit) word |= 0x8000 >> c;
			}
			const off = base + (row * planes + pl) * srcBytesPerRow;
			out[off] = (word >> 8) & 0xff; out[off + 1] = word & 0xff;
		}
	}
	return { data: out, height };
}

// ---------------------------------------------------------------------------
// REDIMENSIONADO DE CALIDAD (Lanczos-3 / area / bilinear / nearest)
// ---------------------------------------------------------------------------
const sinc = (x) => (x === 0 ? 1 : Math.sin(Math.PI * x) / (Math.PI * x));
function lanczosW(x, a = 3) { if (x <= -a || x >= a) return 0; return sinc(x) * sinc(x / a); }
function boxW(x) { return Math.abs(x) < 0.5 ? 1 : 0; } // nearest
function triW(x) { const a = Math.abs(x); return a < 1 ? 1 - a : 0; } // bilinear

// Reescala un canal 2D (separado) de w0->w1. `kernel` ofrece pesos por desplazamiento.
function resample1d(src, w, h, dst, w1, xf, kernel, box) {
	for (let y = 0; y < h; y++) {
		const sy = y * w, dy = y * w1;
		for (let i = 0; i < w1; i++) {
			const x = (i + 0.5) * xf - 0.5; // posición en el origen (xf = src/dst: MULTIPLICAR)
			if (box) {
				const a = Math.floor(i * xf), b = Math.min(w - 1, Math.floor((i + 1) * xf - 1e-9) + 1);
				const lo = Math.max(0, Math.floor(x)), hi = Math.min(w, Math.ceil(x) + 1);
				let s = 0, n = 0;
				const s0 = Math.max(0, a), s1 = Math.min(w, b);
				for (let k = s0; k < s1; k++) { s += src[sy + k]; n++; }
				dst[dy + i] = n ? s / n : src[sy + Math.max(0, Math.min(w - 1, a))];
				continue;
			}
			const r = (kernel === lanczosW) ? 3 : 1;
			let acc = 0, wsum = 0;
			for (let k = Math.ceil(x - r); k <= Math.floor(x + r); k++) {
				const ww = kernel(x - k);
				if (ww === 0) continue;
				const sIdx = Math.max(0, Math.min(w - 1, k));
				acc += src[sy + sIdx] * ww; wsum += ww;
			}
			dst[dy + i] = wsum ? acc / wsum : src[sy + Math.max(0, Math.min(w - 1, Math.round(x)))];
		}
	}
}

// Redimensiona EJES 'columnas' (vertical) sobre una imagen row-major [h filas][w cols] ->
// [h1 filas][w cols] (out). Escrita explícitamente para columnas; la genérica
// resample1d solo sirve para el eje horizontal.
function resampleCols(src, w, h, dst, h1, yf, kernel, box) {
	const r = (kernel === lanczosW) ? 3 : 1;
	const acc = new Float32Array(w);
	for (let oy = 0; oy < h1; oy++) {
		for (let c = 0; c < w; c++) acc[c] = 0;
		const y = (oy + 0.5) * yf - 0.5; // posición en el origen (yf = H/H1: MULTIPLICAR)
		if (box) {
			const a = Math.floor(oy * yf), b = Math.min(h - 1, Math.floor((oy + 1) * yf - 1e-9) + 1);
			const s0 = Math.max(0, a), s1 = Math.min(h, b + 1);
			const n = Math.max(1, s1 - s0);
			for (let k = s0; k < s1; k++) for (let c = 0; c < w; c++) acc[c] += src[k * w + c];
			for (let c = 0; c < w; c++) dst[oy * w + c] = acc[c] / n;
			continue;
		}
		let wsum = 0;
		for (let k = Math.ceil(y - r); k <= Math.floor(y + r); k++) {
			const ww = kernel(y - k);
			if (ww === 0) continue;
			const ck = Math.max(0, Math.min(h - 1, k));
			for (let c = 0; c < w; c++) acc[c] += src[ck * w + c] * ww;
			wsum += ww;
		}
		if (wsum) {
			for (let c = 0; c < w; c++) dst[oy * w + c] = acc[c] / wsum;
		} else {
			const ck = Math.max(0, Math.min(h - 1, Math.round(y)));
			for (let c = 0; c < w; c++) dst[oy * w + c] = src[ck * w + c];
		}
	}
}

// Reescala RGBA a (W1xH1). method: lanczos|area|bilinear|nearest
function resizeImage(src, W, H, W1, H1, method) {
	if (W1 === W && H1 === H) return src;
	const xf = W / W1, yf = H / H1;
	let kernel = triW;
	if (method === 'lanczos') kernel = lanczosW;
	else if (method === 'nearest') kernel = boxW;
	else if (method === 'area') kernel = boxW;
	const boxArea = method === 'area';
	const planes = [0, 1, 2, 3].map((ch) => {
		const a = new Float32Array(W * H);
		for (let i = 0; i < a.length; i++) a[i] = src.data[i * 4 + ch];
		const tmp = new Float32Array(W1 * H);
		const out = new Float32Array(W1 * H1);
		resample1d(a, W, H, tmp, W1, xf, kernel, boxArea);
		resampleCols(tmp, W1, H, out, H1, yf, kernel, boxArea);
		return out;
	});
	const out = Buffer.alloc(W1 * H1 * 4);
	for (let i = 0; i < W1 * H1; i++) for (let ch = 0; ch < 4; ch++) {
		const v = Math.round(planes[ch][i]);
		out[i * 4 + ch] = v < 0 ? 0 : v > 255 ? 255 : v;
	}
	return { width: W1, height: H1, data: out };
}

// ---------------------------------------------------------------------------
// OLLAMA LOCAL (visión) — describe la imagen final
// ---------------------------------------------------------------------------
async function ollamaDescribe(png, model, base, maxTok) {
	const pngBytes = PNG.sync.write(png);
	const b64 = pngBytes.toString('base64');
	const body = { model, temperature: 0.2, max_tokens: maxTok || 300,
		messages: [{ role: 'user', content: [
			{ type: 'text', text: 'Describe brevemente la imagen (contenido, estilo, colores dominantes).' },
			{ type: 'image_url', image_url: { url: `data:image/png;base64,${b64}` } },
		] }] };
	const r = await fetch(`${base}/v1/chat/completions`, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) });
	if (!r.ok) throw new Error(`ollama respondió ${r.status}: ${await r.text()}`);
	const j = await r.json();
	return j.choices?.[0]?.message?.content ?? JSON.stringify(j);
}

// ---------------------------------------------------------------------------
// EXTRACCIÓN DE PLANOS/BANDAS (para fondos continuos tipo Metal Slug)
// Detecta saltos significativos del color medio por fila y devuelve bandas
// horizontales a ancho completo. Escribe cada banda + bands.json + preview.
// ---------------------------------------------------------------------------
function extractBands(png, outDir, jumpTh, step, align) {
	const W = png.width, H = png.height;
	step = Math.max(1, step || 32);
	const means = [];
	for (let y0 = 0; y0 < H; y0 += step) {
		const y1 = Math.min(H, y0 + step);
		let r = 0, g = 0, b = 0, n = 0;
		for (let y = y0; y < y1; y++) for (let x = 0; x < W; x += 4) {
			const o = (y * W + x) * 4; r += png.data[o]; g += png.data[o + 1]; b += png.data[o + 2]; n++;
		}
		means.push([y0, y1, r / n, g / n, b / n]);
	}
	// Saltos entre bandas consecutivas
	const cuts = [0];
	for (let i = 1; i < means.length; i++) {
		const d = Math.abs(means[i][2] - means[i - 1][2]) + Math.abs(means[i][3] - means[i - 1][3]) + Math.abs(means[i][4] - means[i - 1][4]);
		if (d >= jumpTh) cuts.push(means[i][0]);
	}
	cuts.push(H);
	if (align) { for (let i = 1; i < cuts.length - 1; i++) { cuts[i] = Math.round(cuts[i] / align) * align; } }
	// únicas y ordenadas
	const uniq = [...new Set(cuts)].sort((a, b) => a - b);
	const rects = [];
	for (let i = 0; i + 1 < uniq.length; i++) {
		const y0 = uniq[i], y1 = uniq[i + 1];
		if (y1 - y0 >= step) rects.push({ x: 0, y: y0, w: W, h: y1 - y0 });
	}
	fs.mkdirSync(outDir, { recursive: true });
	const cells = [];
	rects.forEach((rc, i) => {
		const crop = Buffer.alloc(rc.w * rc.h * 4);
		for (let y = 0; y < rc.h; y++) {
			const src = ((rc.y + y) * W + rc.x) * 4;
			png.data.copy(crop, y * rc.w * 4, src, src + rc.w * 4);
		}
		const name = `band_${String(i).padStart(2, '0')}_y${rc.y}-${rc.y + rc.h}.png`;
		fs.writeFileSync(path.join(outDir, name), PNG.sync.write({ width: rc.w, height: rc.h, data: crop }));
		cells.push({ name, rect: rc });
	});
	// preview (hoja de contacto vertical)
	const cw = Math.min(W, 256);
	const totalH = rects.reduce((s, r) => s + r.h, 0);
	const sf = totalH > 3000 ? 3000 / totalH : 1;
	const ph = Math.max(1, Math.round(totalH * sf));
	const prev = new PNG({ width: cw, height: ph });
	for (let i = 0; i < rects.length; i++) {
		const rc = rects[i]; const hh = Math.max(1, Math.round(rc.h * sf));
		for (let y = 0; y < hh; y++) for (let x = 0; x < cw; x++) {
			const o1 = ((rc.y + Math.floor(y / sf)) * W + Math.floor(x / sf)) * 4;
			const o2 = (y * cw + x) * 4;
			prev.data[o2] = png.data[o1]; prev.data[o2 + 1] = png.data[o1 + 1]; prev.data[o2 + 2] = png.data[o1 + 2]; prev.data[o2 + 3] = 255;
		}
	}
	fs.writeFileSync(path.join(outDir, 'bands_preview.png'), PNG.sync.write(prev));
	fs.writeFileSync(path.join(outDir, 'bands.json'), JSON.stringify({ step, cuts: uniq, cells }, null, 2), 'utf8');
	return { rects, totalH, cuts: uniq };
}

// ---------------------------------------------------------------------------
// PARSER DE PROPUESTAS DEL VLM (ops.txt de run-vision-verify) y aplicación
// Recorta los trozos que el modelo ve (coords %) y aplica el color transparente.
// ---------------------------------------------------------------------------
function parseOps(text) {
	const rects = [];
	// Robusto ante '0-15% (X), 0-30% (Y)' (dash) y '35%-65% en X, 15%-45% en Y'
	// (porcentaje entre ambos números): captura el nombre y los 4 números ignorando
	// los separadores (%, -, en, (X), comas).
	const lineRe = /^[\s•\-*]*([^:]{2,60}?):\s*([\d.]+)[^\d]*?([\d.]+)[^\d]*?([\d.]+)[^\d]*?([\d.]+)/;
	for (const line of text.split(/\r?\n/)) {
		const m = line.match(lineRe);
		if (!m) continue;
		let a = +m[2], b = +m[3], c = +m[4], d = +m[5];
		if (a > b) { const t = a; a = b; b = t; }   // por si vienen desordenados
		if (c > d) { const t = c; c = d; d = t; }
		rects.push({ name: m[1].trim(), x0: a, x1: b, y0: c, y1: d });
	}
	let trans = null;
	const cm = text.match(/transparente[^:(]*\(?\s*([\d]+)\s*,\s*([\d]+)\s*,\s*([\d]+)\s*\)?/i);
	if (cm) trans = [+cm[1], +cm[2], +cm[3]];
	else {
		const nm = text.match(/transparente[^:]*:\s*([a-záéíóúñ ]+)/i);
		if (nm) {
			const n = nm[1].toLowerCase();
			if (/negro|black|fondo/.test(n)) trans = [0, 0, 0];
			else if (/blanco|white/.test(n)) trans = [255, 255, 255];
			else if (/magenta|fucsia/.test(n)) trans = [255, 0, 255];
		}
	}
	return { rects, trans };
}
function sanitizeName(s) { return s.replace(/[^a-z0-9ÁÉÍÓÚáéíóúñÑ_ -]/gi, '').trim().replace(/\s+/g, '_').slice(0, 48); }

// ---------------------------------------------------------------------------
// EMPAQUETADO POR PROFUNDIDAD (bits/píxel) — índice → flujo de bits LSB-first
// ---------------------------------------------------------------------------
function packIndices(indices, bits) {
	const out = Buffer.alloc(Math.ceil(indices.length * bits / 8), 0);
	let bitpos = 0;
	for (const v of indices) { for (let b = 0; b < bits; b++) { if (v & (1 << b)) out[bitpos >> 3] |= (1 << (bitpos & 7)); bitpos++; } }
	return out;
}

// ---------------------------------------------------------------------------
// ORQUESTACIÓN (main) — este fichero ES el CLI de amiga-tiles (ver README.md).
// Flujo de llamadas dentro de main(), en orden:
//   arg()/intArg()/floatArg()      parseo de opciones de la línea de comandos
//   loadImage()                    PUEDE ser JPEG (jpeg-js) o PNG (pngjs)
//   crop()                         -> resizeImage()/emit-source (opcionales)
//   parseOps()                     "recortar según el VLM" (--ops)
//   quantizeIndexed()              paleta: kmeans/medianCut/brightSplit/
//                                  popularityPalette/fixedCube/fixedGrays/
//                                  tabla externa; dither none/floyd/atkinson/
//                                  bayer/best; EHB half-aware (buildTable)
//   sliceTiles()+mergeSimilar()    tilebank + dedupe
//   reconstructAndCompare()        assert al 100% (sin merge)
//   writeIndexedPng()/verifyPng()  reconstruct/tilebank + round-trip (crc32)
//   packIndices()                  banco empaquetado por bits (--pack)
//   emitXlimitedBank()             solo con --xlimited
//   extractBands()                 solo con --extract-bands DIR
//   ollamaDescribe()               solo con --describe
// Este CLI también se lanza COMO SUBPROCESO desde tools/amiga-tiles/game-assets.mjs
// (paso --quantize, siempre con --alpha para reservar el índice 0).
// ---------------------------------------------------------------------------
function fail(msg) { console.error(`[amiga-tiles] ${msg}`); process.exit(1); }

async function main() {
	const input = process.argv[2];
	if (!input || has('--help') || has('-h')) {
		const readme = fs.readFileSync(path.join(__dirname, 'README.md'), 'utf8').split('\n').slice(0, 40).join('\n');
		console.log(readme.split('```')[0].trim());
		process.exit(input ? 0 : 1);
	}
	if (!fs.existsSync(input)) fail(`no existe ${input}`);

	const colorsArg = intArg('--colors', 0);
	const flagsShort = has('--no-alpha') ? 'none' : (has('--alpha') ? 'always' : 'auto');
	let dither = arg('--dither', 'none');
	let ditherDeadband = Math.max(0, floatArg('--dither-threshold', 0)); // no difundir errores < umbral (zonas planas)
	let ditherClamp = Math.max(0, floatArg('--dither-clamp', 0));        // capa el error acumulado por canal
	let serpentineMode = has('--serpentine');                            // escaneo en zigzag (rompe gusanos)
	const strength = Math.max(0, Math.min(1, floatArg('--dither-strength', 1)));
	// 'best' = Floyd + serpentina + deadband + clamp: ideal para fotos continuas
	// (evita el punteado visible en zonas planas de color casi uniforme, p. ej. cielos).
	if (dither === 'best') {
		dither = 'floyd';
		if (!has('--dither-threshold')) ditherDeadband = 14;
		if (!has('--dither-clamp')) ditherClamp = 16;
		if (!has('--serpentine')) serpentineMode = true;
	}
	if (!['none', 'floyd', 'atkinson', 'bayer'].includes(dither)) fail(`--dither inválido: ${dither}`);
	let palSrc = arg('--palette', 'adaptive');
	// --perceptual activa la cuantización "half-aware + ponderada" de forma global:
	// histograma ponderado por luminancia/croma + métrica perceptual (distP) en el
	// clustering y en el remap final. --palette perceptual es un alias conveniente
	// que, además, fuerza el refinamiento de half-brights (ehbPaletteBright) en EHB.
	if (has('--perceptual') || palSrc === 'perceptual') PERCEPTUAL = true;
	if (palSrc === 'perceptual') palSrc = 'ehb';
	const sortPal = arg('--sort', 'luminance');
	const tile = Math.max(1, intArg('--tile', 16));
	const mergeF = floatArg('--merge', 1);
	const sheetScale = Math.max(1, intArg('--sheet-scale', 1));
	const emitXl = has('--xlimited');
	const kIters = Math.max(1, intArg('--palette-k', 12));
	const packArg = arg('--pack', 'auto');                 // auto|on|off: empaqueta índices a bits(=ceil(log2 colors))
	if (!['auto', 'on', 'off'].includes(packArg)) fail(`--pack inválido: ${packArg}`);
	const outDirArg = arg('--out', '');
	const outDir = outDirArg ? path.resolve(outDirArg) : path.join(path.dirname(path.resolve(input)), 'out');
	fs.mkdirSync(outDir, { recursive: true });

	let png = loadImage(input);
	let W = png.width, H = png.height;

	// ---- Extracción de bandas (fondos continuos) sobre la imagen ORIGINAL -------
	const bandsDir = arg('--extract-bands', '');
	if (bandsDir) {
		const bands = extractBands(png, path.resolve(bandsDir), floatArg('--band-jump', 60), intArg('--band-step', 32), intArg('--band-align', 0));
		console.log(`[amiga-tiles] extract-bands: ${bands.rects.length} bandas -> ${path.resolve(bandsDir)}`);
		for (const c of bands.rects) console.log(`   y=${c.y}..${c.y + c.h} (${c.h}px)`);
		return;
	}

	// ---- Crop opcional ----------------------------------------------------------
	const cropArg = arg('--crop', '');
	if (cropArg) {
		const [cx, cy, cw, ch] = cropArg.split(/[\s,x]/).map(Number);
		if (![cx, cy, cw, ch].every(Number.isFinite)) fail(`--crop inválido: ${cropArg} (X,Y,W,H)`);
		const x0 = Math.max(0, cx), y0 = Math.max(0, cy);
		const x1 = Math.min(W, cx + cw), y1 = Math.min(H, cy + ch);
		const crop = Buffer.alloc((x1 - x0) * (y1 - y0) * 4);
		for (let y = y0; y < y1; y++) png.data.copy(crop, (y - y0) * (x1 - x0) * 4, (y * W + x0) * 4, (y * W + x1) * 4);
		png = { width: x1 - x0, height: y1 - y0, data: crop }; W = png.width; H = png.height;
	}

	// ---- Redimensionado de calidad ----------------------------------------------
	const resizeArg = arg('--resize', '');
	const maxArea = intArg('--max-area', 0);
	const maxRam = intArg('--max-ram', 0);
	const resample = arg('--resample', 'lanczos');
	if (!['lanczos', 'area', 'bilinear', 'nearest'].includes(resample)) fail(`--resample inválido: ${resample}`);
	let tw_ = W, th_ = H; // dimensiones de trabajo
	if (resizeArg) {
		const [rW, rH] = resizeArg.split(/[\s,x]/).map(Number);
		if (![rW, rH].every(Number.isFinite) || rW <= 0 || rH <= 0) fail(`--resize inválido: ${resizeArg} (WxH)`);
		tw_ = Math.round(rW); th_ = Math.round(rH);
	} else if (maxArea > 0 || maxRam > 0) {
		const area = maxArea > 0 ? maxArea : maxRam;          // 1 B/píxel de índice ≈ N bytes de RAM
		const scale = Math.sqrt(area / (W * H));
		if (scale < 1) { tw_ = Math.max(1, Math.round(W * scale)); th_ = Math.max(1, Math.round(H * scale)); }
	}
	// Ajusto a múltiplo de `tile` para que el slicing/split siempre funcione, y
	// garantizo que el área no supere el presupuesto pedido (--max-area/--max-ram).
	tw_ = Math.max(tile, Math.ceil(tw_ / tile) * tile);
	th_ = Math.max(tile, Math.ceil(th_ / tile) * tile);
	const budget = maxArea > 0 ? maxArea : maxRam;
	while (budget > 0 && tw_ * th_ > budget && (tw_ > tile || th_ > tile)) {
		if (tw_ >= th_) tw_ -= tile; else th_ -= tile;
	}
	if (tw_ !== W || th_ !== H) {
		const before = (W * H) >>> 0;
		png = resizeImage(png, W, H, tw_, th_, resample);
		W = png.width; H = png.height;
		console.log(`[amiga-tiles] redimensionado ${W}x${H} (${before} → ${W * H} px, ${resample})`);
	}

	// Guarda la imagen de trabajo (post crop/resize) y sale; útil para demostrar
	// el redimensionado o preparar una región para inspección.
	if (has('--emit-source')) {
		const emName = `source_resized_${W}x${H}_${resample}.png`;
		fs.writeFileSync(path.join(outDir, emName), PNG.sync.write(png));
		console.log(`[amiga-tiles] --emit-source -> ${path.join(outDir, emName)} (${W}x${H})`);
		return;
	}

	// ---- Aplicar propuestas del VLM (--ops ops.txt) --------------------------
	// Extrae los trozos que el modelo de visión propone (coords % del ops.txt) y
	// aplica el color transparente sugerido. Escribe PNGs + ops.json en
	// <out>/extract y sale.
	const opsFile = arg('--ops', '');
	if (opsFile) {
		const ops = parseOps(fs.readFileSync(opsFile, 'utf8').replace(/^\uFEFF/, ''));
		const opsDir = path.join(outDir, 'extract');
		fs.mkdirSync(opsDir, { recursive: true });
		const json = [];
		for (let i = 0; i < ops.rects.length; i++) {
			const r = ops.rects[i];
			const x0 = Math.round(W * r.x0 / 100), x1 = Math.round(W * r.x1 / 100);
			const y0 = Math.round(H * r.y0 / 100), y1 = Math.round(H * r.y1 / 100);
			const cw = x1 - x0, ch = y1 - y0;
			if (cw < 4 || ch < 4) continue;
			const crop = Buffer.alloc(cw * ch * 4);
			for (let y = y0; y < y1; y++) png.data.copy(crop, (y - y0) * cw * 4, (y * W + x0) * 4, (y * W + x1) * 4);
			const name = `ops_${String(i).padStart(2, '0')}_${sanitizeName(r.name)}`;
			if (ops.trans) {
				const [tr, tg, tb] = ops.trans;
				for (let p = 0; p < cw * ch; p++) {
					const o = p * 4;
					if (Math.abs(crop[o] - tr) <= 8 && Math.abs(crop[o + 1] - tg) <= 8 && Math.abs(crop[o + 2] - tb) <= 8) crop[o + 3] = 0;
				}
			}
			fs.writeFileSync(path.join(opsDir, name + '.png'), PNG.sync.write({ width: cw, height: ch, data: crop }));
			json.push({ name, rect: { x: x0, y: y0, w: cw, h: ch }, rectangular: true, transparent: ops.trans || null });
		}
		fs.writeFileSync(path.join(opsDir, 'ops.json'), JSON.stringify(json, null, 2), 'utf8');
		console.log(`[amiga-tiles] --ops: ${json.length} trozos extraídos -> ${opsDir}`);
		for (const j of json) console.log(`   ${j.name} ${j.rect.w}x${j.rect.h}${j.transparent ? '  transp=' + j.transparent.join(',') : ''}`);
		return;
	}

	// ---- Verificar divisibilidad y analizar transparencia ------------------------
	const { pct } = analyze(png);
	if (W % tile || H % tile) fail(`dimensiones (${W}x${H}) no múltiplo de tile ${tile}; usa --resize/--max-area/--crop o --tile adecuado`);
	let alpha = flagsShort === 'always' ? true : flagsShort === 'none' ? false : pct >= 0.5;
	console.log(`[amiga-tiles] ${input} ${W}x${H} tiles ${tile} (transparencia ${pct.toFixed(2)}%)`);

	// Profundidad: auto por nº de colores únicos del original.
	const hist = histogram(png, alpha, PERCEPTUAL);
	const uniqueColors = hist.length;
	let colors = colorsArg;
	if (!colorsArg) {
		colors = uniqueColors <= 4 ? 4 : uniqueColors <= 8 ? 8 : uniqueColors <= 16 ? 16 : uniqueColors <= 32 ? 32 : 64;
		console.log(`[amiga-tiles] --colors no dado; ${uniqueColors} colores únicos → ${colors}${colors === 64 ? ' (EHB)' : ''}`);
	}
	// Deducción de transparencia: si `colors` = 2^n - 1 (3,7,15,31,63…) significa
	// "quiero `colors` colores reales y un slot 0 transparente", o sea `colors+1`
	// slots (2^n) con el índice 0 reservado. El programa lo deduce solo (a no ser
	// que se fuerce --no-alpha). Así `--colors 31` ⇒ 31 colores + transparente.
	const powMinus1 = colors >= 1 && (colors & (colors + 1)) === 0; // (colors+1) es potencia de 2
	if (powMinus1 && flagsShort !== 'none') {
		if (!alpha) console.log(`[amiga-tiles] ${colors} = 2^n−1 ⇒ reservo índice 0 transparente (${colors + 1} slots, ${Math.ceil(Math.log2(colors + 1))} bits/px)`);
		alpha = true;                 // reservar el índice 0
		colors = colors + 1;          // slots totales = potencia de 2; colores reales = colors-1
	}
	// Recomendación de profundidad basada en el histograma real (ayuda a elegir).
	{
		const rec = uniqueColors <= 4 ? 4 : uniqueColors <= 8 ? 8 : uniqueColors <= 16 ? 16 : uniqueColors <= 32 ? 32 : 64;
		const isPow2 = rec >= 1 && (rec & (rec - 1)) === 0;
		const alternative = isPow2 && rec >= 4 ? ` (para transparencia usa ${rec - 1}, es 2^n−1)` : '';
		console.log(`[amiga-tiles] recomendación: ${rec} colores → ${Math.ceil(Math.log2(rec))} bits/px${alternative}`);
	}
	const { bits, ehb } = modeOf(colors);
	// Nº de colores REALES que hay que elegir para llenar la tabla efectiva:
	//  - EHB: 32 bases siempre (el base 0 queda reservado si hay transparencia).
	//  - resto: `colors`, o `colors-1` si el slot 0 es transparencia.
	const baseCount = ehb ? 32 : (colors - (alpha ? 1 : 0));
	if (baseCount < 1) fail('--colors demasiado pequeño para reservar transparencia');

	// ---- Paleta ------------------------------------------------------------------
	let bases = null; let palKind = palSrc; let palNote = '';
	if (['adaptive', 'kmeans'].includes(palSrc)) { bases = kmeans(hist, baseCount, kIters, ehb); palNote = `kmeans${ehb ? ' half-aware' : ''}`; }
	else if (palSrc === 'mediancut') { bases = medianCut(hist, baseCount); palNote = 'median-cut'; }
	else if (palSrc === 'bright') {
		if (!ehb) console.warn('[warn] --palette bright solo tiene sentido en EHB; se usa median-cut');
		bases = brightSplit(hist, baseCount); palNote = 'mitad brillante → bases';
	}
	else if (palSrc === 'ehb') {
		if (!ehb) console.warn('[warn] --palette ehb solo tiene sentido en EHB; se usa kmeans half-aware');
		bases = ehbPaletteBright(hist, baseCount, kIters); palNote = 'EHB half-max (brillos ajustados a los half)';
	}
	else if (palSrc === 'popularity') { bases = popularityPalette(hist, baseCount); palNote = 'popularity'; }
	else if (palSrc === 'cube' || palSrc === 'halftone') { bases = fixedCubePalette(baseCount); palNote = 'malla fija RGB444'; }
	else if (palSrc === 'grays') { bases = fixedGraysPalette(baseCount); palNote = 'grises'; }
	else if (/\.(json|act|pal)$/i.test(palSrc)) {
		const raw = JSON.parse(fs.readFileSync(palSrc, 'utf8').replace(/^\uFEFF/, ''));
		const list = Array.isArray(raw) ? raw : (raw.colors || raw.palette);
		// soporta objetos {r,g,b} y arrays [r,g,b]
		bases = list.slice(0, baseCount).map((e) => Array.isArray(e) ? [e[0], e[1], e[2]] : [e.r, e.g, e.b]);
		if (bases.length < baseCount) { console.warn(`[warn] paleta externa trae ${bases.length}, se rellena con grises`); const g = fixedGraysPalette(baseCount); for (let i = bases.length; i < baseCount; i++) bases.push(g[i]); }
		palNote = `externa ${palSrc}`;
	}
	else { fail(`--palette desconocido: ${palSrc} (adaptive|kmeans|mediancut|bright|ehb|perceptual|popularity|cube|halftone|grays|<archivo>)`); }
	// Garantiza el nº exacto de bases (p. ej. bright/mediancut pueden devolver
	// menos de los pedidos según la imagen): se completa con los colores más
	// frecuentes y se eliminan casi-duplicados.
	bases = fillCenters(bases, hist, baseCount);
	bases = bases.slice(0, baseCount);
	if (sortPal === 'luminance' && !(ehb && palSrc === 'bright')) bases.sort((a, b) => lum(a) - lum(b));

	const table = buildTable(colors, bases, alpha);
	const paletteFinal = tableAsPalette(table);

	// ---- Cuantización + dithering ------------------------------------------------
	const indexed = quantizeIndexed(png, table, alpha, dither, strength, { deadband: ditherDeadband, clamp: ditherClamp, serpentine: serpentineMode });

	// ---- Slice / dedupe / merge / comparar ---------------------------------------
	let { bank, map, cols, rows } = sliceTiles(indexed, W, H, tile);
	let absorbed = 0, mergeNote = '(exacto)';
	if (mergeF < 1) { const r = mergeSimilar(bank, map, cols, mergeF); bank = r.bank; absorbed = r.absorbed; mergeNote = `(>=${mergeF} iguales; -${r.absorbed})`; }
	const cmp = reconstructAndCompare(indexed, W, H, bank, map, cols, tile);
	const cellsTotal = cols * rows;
	const rep = 1 - bank.length / cellsTotal;
	console.log(`[amiga-tiles] tiles únicos: ${bank.length} de ${cellsTotal} ${mergeNote}`);
	// Patrón de repetición: en una foto/degradado casi todos los tiles son únicos y
	// el dedupe no aporta nada (tilebank.bin ≈ imagen indexada y tilebank.png ≈
	// reconstruct.png). Es una señal para el usuario, no un error.
	if (mergeF >= 1) {
		if (bank.length >= Math.ceil(cellsTotal * 0.85)) {
			console.log(`[amiga-tiles] AVISO: sin patrón de repetición (${bank.length}/${cellsTotal} celdas únicas, ${(rep * 100).toFixed(1)}% duplicadas). El tilebank equivale a la imagen: tilebank.bin == imagen indexada y tilebank.png ≈ reconstruct.png.`);
		} else if (bank.length >= Math.ceil(cellsTotal * 0.6)) {
			console.log(`[amiga-tiles] nota: poca repetición (${(rep * 100).toFixed(1)}% de celdas duplicadas); el tilebank aún tiene redundancia útil.`);
		}
	}
	console.log(`[amiga-tiles] COMPARAR (original cuantizado vs reconstruido): ${cmp.pct.toFixed(2)}% de ${W * H} índices`);
	if (mergeF >= 1 && cmp.pct < 100) fail(`sin fusión la reconstrucción debe cuadrar al 100% (${cmp.pct.toFixed(2)})`);

	// ---- Salidas ------------------------------------------------------------------
	const baseName = path.basename(input, path.extname(input));
	// tilebank.bin (1 B/px, stride fijo) + .h
	const tech = ehb ? 'ehb' : (palSrc === 'adaptive' ? 'kmeans' : palSrc);
	let suf = `${colors}c_${tech}_${dither}_${W}x${H}`;
	if (serpentineMode) suf += '_serp';
	if (ditherDeadband > 0) suf += `_dth${Math.round(ditherDeadband)}`;
	if (ditherClamp > 0) suf += `_cl${Math.round(ditherClamp)}`;
	let label = `colors=${colors} technique=${tech} dither=${dither} resolution=${W}x${H} palette=${palSrc} alpha=${alpha ? 'yes' : 'no'}`;
	if (serpentineMode) label += ' serpentine=yes';
	if (ditherDeadband > 0) label += ` threshold=${ditherDeadband.toFixed(1)}`;
	if (ditherClamp > 0) label += ` clamp=${ditherClamp.toFixed(0)}`;

	// ---- Salidas ------------------------------------------------------------------
	// Empaquetado por profundidad: si bits(=ceil log2 colors) cabe en <1 B se envasa
	// (auto: packs cuando bits <= 4 => 10 colores -> 4 bits -> 2 px/byte). La regla
	// del repo está en docs (transparencia+empaquetado). El stride es POR TILE.
	const packMode = packArg === 'on' ? true : (packArg === 'off' ? false : bits <= 4);
	const packedPerTile = packMode ? Math.ceil((tile * tile * bits) / 8) : tile * tile;
	const bin = Buffer.alloc(bank.length * packedPerTile);
	for (let i = 0; i < bank.length; i++) {
		if (packMode) packIndices(bank[i].pix, bits).copy(bin, i * packedPerTile);
		else for (let q = 0; q < tile * tile; q++) bin[i * packedPerTile + q] = bank[i].pix[q];
	}
	const binPath = path.join(outDir, `tilebank_${suf}.bin`); fs.writeFileSync(binPath, bin);

	const hLines = [];
	hLines.push('// Tilebank indexado generado por tools/amiga-tiles/amiga-tiles.mjs.');
	hLines.push(`// Conversion: ${label}`);
	hLines.push(`// ${bits} bits/píxel (${colors} colores${ehb ? ' EHB: 0..31 base, 32..63 half' : ''}), ${alpha ? 'índice 0 = transparente' : 'sin transparencia'}.`);
	hLines.push(`// Empaquetado: ${packMode ? 'SÍ (' + (8 / bits) + ' píxeles/byte, bits LSB-first)' : 'no (1 byte/píxel)'}. Stride por tile = ${packedPerTile} bytes.`);
	hLines.push(`// ${bank.length} tiles de ${tile}x${tile}, mapa ${cols}x${rows}. Paleta: ${palNote}.`);
	hLines.push('// Metadatos completos en los ficheros palette_' + suf + '.json y en el tEXt de los PNG.');
	hLines.push(`static const unsigned char kPalette[${colors * 3}] = {`);
	for (let r = 0; r < colors; r += 12) hLines.push('  ' + paletteFinal.slice(r, r + 12).map((c) => `${c[0]},${c[1]},${c[2]}`).join(',') + ',');
	hLines.push('};');
	hLines.push(`// Datos en "tilebank_${suf}.bin" (incbin).`);
	hLines.push(`static const unsigned char kTileBankBitsPerPixel = ${bits};`);
	hLines.push(`static const unsigned short kTileBankStride = ${packedPerTile};`);
	hLines.push(`static const unsigned int kTileBankBytes = ${bin.length};`);
	hLines.push(`static const unsigned short kTileIndexedMap[${map.length}] = {`);
	for (let i = 0; i < map.length; i += 24) hLines.push('  ' + map.slice(i, i + 24).join(',') + ',');
	hLines.push('};');
	fs.writeFileSync(path.join(outDir, `tilebank_${suf}.h`), hLines.join('\n') + '\n', 'utf8');

	// Gráfico de uso de la paleta: tira de rectángulos por color + histograma del
	// uso real de cada slot en la imagen convertida (PNG de inspección).
	const chart = paletteChartPng(outDir, suf, label, paletteFinal, indexed, alpha, bits, ehb, colors, has('--chart-compact'));

	// palette.json / palette.h
	const palJson = JSON.stringify({ name: suf, label: label, tile, cols, rows, colors, bits, bitsPerPixel: bits, packed: packMode, stridePerTile: packedPerTile, ehb, alpha, method: palNote, palette: paletteFinal, bank: bank.map((b, i) => ({ pix: [...b.pix] })), map, stats: { unique: bank.length, cells: cols * rows }, hist: [...chart.counts] }, null, 2);
	fs.writeFileSync(path.join(outDir, `palette_${suf}.json`), palJson, 'utf8');

	const words = paletteFinal.map(to444);
	const phLines = ['// Paleta Amiga (0x0RGB). Conversion: ' + label, '// ' + (ehb ? 'EHB: bases 0..31, half 32..63 generados por hardware.' : `${colors} colores.`)];
	for (let r = 0; r < colors; r += 8) phLines.push('    ' + words.slice(r, r + 8).map((w) => `0x${w.toString(16).padStart(3, '0')}`).join(', ') + ',');
	fs.writeFileSync(path.join(outDir, `palette_${suf}.h`), phLines.join('\n') + '\n', 'utf8');

	// PNG indexados + verificación round-trip (con tEXt de conversión)
	const reconPng = path.join(outDir, `reconstruct_${suf}.png`);
	writeIndexedPng(reconPng, paletteFinal, cmp.recon, W, H, label);
	const rv = verifyPng(reconPng, paletteFinal, cmp.recon, W, H);
	console.log(`[verify] ${path.basename(reconPng)} colorType=${rv.colorType} roundtripDiffs=${rv.dif} -> ${rv.dif === 0 ? 'OK' : 'FALLO'}`);

	const perRow = Math.max(1, Math.floor((W / tile) / sheetScale));
	const sw = tile * sheetScale;
	const cW = perRow * sw, cH = sw * Math.ceil(bank.length / perRow);
	const inds = new Uint8Array(cW * cH);
	for (let i = 0; i < bank.length; i++) {
		const ox = (i % perRow) * sw, oy = Math.floor(i / perRow) * sw;
		for (let yy = 0; yy < tile; yy++) for (let xx = 0; xx < tile; xx++) {
			const v = bank[i].pix[yy * tile + xx];
			for (let dy = 0; dy < sheetScale; dy++) for (let dx = 0; dx < sheetScale; dx++) inds[(oy + yy * sheetScale + dy) * cW + (ox + xx * sheetScale + dx)] = v;
		}
	}
	const sheetPath = path.join(outDir, `tilebank_${suf}.png`);
	writeIndexedPng(sheetPath, paletteFinal, inds, cW, cH, label);
	const sv = verifyPng(sheetPath, paletteFinal, inds, cW, cH);
	console.log(`[verify] ${path.basename(sheetPath)} colorType=${sv.colorType} roundtripDiffs=${sv.dif} -> ${sv.dif === 0 ? 'OK' : 'FALLO'}`);

	// Banco X-Limited opcional
	if (emitXl) {
		const xl = emitXlimitedBank(bank, tile, colors, 320);
		const xlPath = path.join(outDir, `tilebank_xlimited_${colors}c_${tech}.bin`);
		fs.writeFileSync(xlPath, xl.data);
		const xlh = [`// Banco X-Limited interleaved (320 px ancho, ${bits} planos). Generado por amiga-tiles.`,
			`// Conversion: ${label}`,
			`// ${bank.length} tiles de ${tile}x${tile}; tile -> (t%${320 / tile}, t/${320 / tile}).`,
			'extern "C" const unsigned char g_tilebank_xlimited[];',
			'extern "C" const unsigned int g_tilebank_xlimited_size;', ''].join('\n');
		fs.writeFileSync(path.join(outDir, `tilebank_xlimited_${colors}c_${tech}.h`), xlh, 'utf8');
		console.log(`[amiga-tiles] banco X-Limited -> ${xlPath} (${xl.data.length} B, ${xl.height} planelíneas)`);
	}

	// ---- Podado de la paleta (--prune) ----------------------------------------
	// Si algunos slots NO se usan en la imagen convertida (histograma = 0) y no son
	// el slot 0 transparente, se generan variantes `_pruned_` con la paleta reducida
	// a los colores realmente usados. Menos slots => menos bits/píxel y menos Chip
	// RAM. El slot 0 se conserva siempre cuando hay alpha (es la marca de transparente).
	const pruning = has('--prune');
	if (pruning) {
		const used = [];
		for (let i = 0; i < colors; i++) if (chart.counts[i] > 0 || (alpha && i === 0)) used.push(i);
		if (used.length < colors) {
			const remap = new Int16Array(colors); remap.fill(-1);
			used.forEach((old, k) => { remap[old] = k; });
			const palP = used.map((i) => paletteFinal[i]);
			const nColors = used.length;
			const nBits = Math.max(1, Math.ceil(Math.log2(nColors)));
			const pPack = packArg === 'on' ? true : (packArg === 'off' ? false : nBits <= 4);
			const pStride = pPack ? Math.ceil((tile * tile * nBits) / 8) : tile * tile;
			// reindexar píxeles, banco y mapa
			const idxP = new Uint8Array(indexed.length);
			for (let i = 0; i < indexed.length; i++) { const v = indexed[i]; idxP[i] = remap[v] >= 0 ? remap[v] : 0; }
			const bankP = bank.map((b) => ({ pix: Uint8Array.from(b.pix, (v) => (remap[v] >= 0 ? remap[v] : 0)) }));
			const mapP = map.map((v) => (remap[v] >= 0 ? remap[v] : 0));
			const pBin = Buffer.alloc(bankP.length * pStride);
			for (let i = 0; i < bankP.length; i++) {
				if (pPack) packIndices(bankP[i].pix, nBits).copy(pBin, i * pStride);
				else for (let q = 0; q < tile * tile; q++) pBin[i * pStride + q] = bankP[i].pix[q];
			}
			// reconstrucción con la paleta reducida
			const reconP = new Uint8Array(W * H);
			const perRowP = Math.max(1, Math.floor((W / tile) / sheetScale));
			const swP = tile * sheetScale;
			const cPW = perRowP * swP, cPH = swP * Math.ceil(bankP.length / perRowP);
			const indsP = new Uint8Array(cPW * cPH);
			for (let yy = 0; yy < H; yy++) for (let xx = 0; xx < W; xx++) {
				const k = Math.floor(yy / tile) * cols + Math.floor(xx / tile);
				const t = mapP[k] >= 0 && mapP[k] < bankP.length ? mapP[k] : 0;
				reconP[yy * W + xx] = bankP[t].pix[(yy % tile) * tile + (xx % tile)];
			}
			// etiqueta y sufijo
			const pLabel = `${label} prune=yes (${colors}->${nColors})`;
			const pSuf = `${nColors}c_${tech}_${dither}_${W}x${H}_pruned`;
			const pPalJson = JSON.stringify({ name: pSuf, label: pLabel, tile, cols, rows, colors: nColors, bits: nBits, bitsPerPixel: nBits, packed: pPack, stridePerTile: pStride, ehb, alpha, method: `${palNote} (podada de ${colors})`, palette: palP, bank: bankP.map((b) => ({ pix: [...b.pix] })), map: mapP, stats: { unique: bankP.length, cells: cols * rows }, hist: used.map((i) => chart.counts[i]) }, null, 2);
			fs.writeFileSync(path.join(outDir, `tilebank_${pSuf}.bin`), pBin);
			fs.writeFileSync(path.join(outDir, `palette_${pSuf}.json`), pPalJson);
			const pWords = palP.map(to444);
			const pph = ['// Paleta Amiga podada (0x0RGB). Conversion: ' + pLabel, '// ' + `${nColors} colores`];
			for (let r = 0; r < nColors; r += 8) pph.push('    ' + pWords.slice(r, r + 8).map((w) => `0x${w.toString(16).padStart(3, '0')}`).join(', ') + ',');
			fs.writeFileSync(path.join(outDir, `palette_${pSuf}.h`), pph.join('\n') + '\n', 'utf8');
			const ph = [
				'// Tilebank podado (solo colores usados). Conversion: ' + pLabel,
				`// ${nBits} bits/píxel (${nColors} colores de ${colors} originales), ${alpha ? 'índice 0 = transparente' : 'sin transparencia'}.`,
				`// Empaquetado: ${pPack ? 'SÍ (' + (8 / nBits) + ' píxeles/byte)' : 'no (1 byte/píxel)'}. Stride por tile = ${pStride} bytes.`,
				`// ${bankP.length} tiles de ${tile}x${tile}, mapa ${cols}x${rows}.`,
				`static const unsigned char kPalette[${nColors * 3}] = {`,
			];
			for (let r = 0; r < nColors; r += 12) ph.push('  ' + palP.slice(r, r + 12).map((c) => `${c[0]},${c[1]},${c[2]}`).join(',') + ',');
			ph.push('};', `// Datos en "tilebank_${pSuf}.bin" (incbin).`,
				`static const unsigned char kTileBankBitsPerPixel = ${nBits};`,
				`static const unsigned short kTileBankStride = ${pStride};`,
				`static const unsigned int kTileBankBytes = ${pBin.length};`);
			fs.writeFileSync(path.join(outDir, `tilebank_${pSuf}.h`), ph.join('\n') + '\n', 'utf8');
			writeIndexedPng(path.join(outDir, `reconstruct_${pSuf}.png`), palP, reconP, W, H, pLabel);
			writeIndexedPng(path.join(outDir, `tilebank_${pSuf}.png`), palP, indsP, cPW, cPH, pLabel);
			paletteChartPng(outDir, pSuf, pLabel, palP, idxP, alpha, nBits, false, nColors, has('--chart-compact'));
			console.log(`[amiga-tiles] PRUNE: ${colors}->${nColors} colores usados (${Math.round((1 - nColors / colors) * 100)}% menos slots, ${bits}->${nBits} bits/px); variantes _pruned_ en ${outDir}`);
		} else {
			console.log('[amiga-tiles] PRUNE: todos los slots se usan, sin variante podada');
		}
	}

	// métricas
	let mse = 0, n = 0;
	for (let i = 0; i < W * H; i++) {
		const t = table.find((e) => e.index === indexed[i]);
		if (!t || t.transparent) continue;
		const src = [png.data[i * 4], png.data[i * 4 + 1], png.data[i * 4 + 2]];
		mse += dist(src, t.rgb); n++;
	}
	mse = n ? mse / n : 0;
	const psnr = 10 * Math.log10(255 * 255 * 3 / (mse + 1e-9));
	console.log(`[amiga-tiles] OK -> ${outDir}`);
	console.log(`[amiga-tiles] paleta ${palNote} · ${colors} colores (${bits} bits${ehb ? '/EHB' : ''}) · dither=${dither}(${strength}) · MSE=${mse.toFixed(1)} PSNR=${psnr.toFixed(1)} dB`);
	console.log(`[amiga-tiles] salidas (sufijo ${suf}): reconstruct_${suf}.png, tilebank_${suf}.png/.bin/.h, palette_${suf}.json/.h, palette_chart_${suf}.png/.txt${emitXl ? `, tilebank_xlimited_${colors}c_${tech}.bin/.h` : ''} (${bits} bits/px, empaquetado ${packedPerTile} B/tile)`);

	// Descripción con ollama local (opcional). Envía la imagen FINAL de trabajo.
	if (has('--describe')) {
		const model = arg('--model', 'qwen3-vl:8b-instruct-q8_0');
		const base = arg('--ollama-base', 'http://127.0.0.1:11434');
		const desc = await ollamaDescribe(png, model, base, intArg('--ollama-tokens', 300));
		const descPath = path.join(outDir, 'image_description.txt');
		const srcAbs = path.resolve(input);
		fs.writeFileSync(descPath, `Fuente original: ${srcAbs}\n\n${desc}\n`, 'utf8');
		console.log(`[amiga-tiles] descripción (${model}) de ${srcAbs}:\n${desc}\n  -> ${descPath}`);
	}
}

main().catch((e) => { console.error(`[amiga-tiles] ERROR: ${e.message}`); console.error(e.stack); process.exit(1); });