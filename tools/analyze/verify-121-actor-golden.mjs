// ---------------------------------------------------------------------------
// Verificador GOLDEN del actor de la demo 121 (subposiciones 0..15)
//
// Compara dos capturas del modo `K121_GOLDEN_ACTORS`:
//   - <golden.png>   : las 16 bandas del actor pintadas a la vez (una por columna limpia)
//   - <pristine.png> : el mismo mundo SIN actores (build con K121_FREEZE+SKIP_ACTOR)
//
// El oráculo es INDEPENDIENTE del motor: reimplementa en JS la banda desplazada
// (dos palabras con `shift` 0..15 desde el patrón de píxeles del actor, bit 15 =
// píxel izquierdo) y decodifica el índice de cada píxel del PNG con la paleta
// identidad del modo golden (índice i -> 12 bits (i<<6)|0x3F).
//
// Comprobaciones (todas píxel a píxel, escala 2x del capture):
//   1. Cada píxel de actor (patrón != 0) de cada subposición k aparece EXACTO en su
//      columna (x = col·16 + k + c, y = 144 + r).
//   2. Dentro de cada ventana de 32 px del blit, lo NO cubierto por el actor coincide
//      con la captura prístina (la máscara no toca el fondo).
//   3. Fuera de las 16 ventanas, ambas capturas son IDÉNTICAS (sin rastros ni
//      corrupción colateral en el resto de la pantalla).
//
// Uso: node tools/analyze/verify-121-actor-golden.mjs <golden.png> <pristine.png> [--json]
// ---------------------------------------------------------------------------
import fs from 'node:fs';
import path from 'node:path';
import { createRequire } from 'node:module';

const require = createRequire(import.meta.url);
const { PNG } = require('pngjs');

const args = process.argv.slice(2).filter((a) => !a.startsWith('--'));
const jsonOut = process.argv.includes('--json');
if (args.length < 2) {
	console.error('uso: node tools/analyze/verify-121-actor-golden.mjs <golden.png> <pristine.png> [--json]');
	process.exit(2);
}
const [goldenPath, pristinePath] = args;

// Geometría de la demo (field -> png 2x). OX calibrado empíricamente contra el render
// del actor congelado (bbox 552 px): el origen del campo en el PNG es (78, 30), es decir
// 39/15 px amiga en el capture 2x (el DIWSTRT izquierdo es impar: 0x81).
// El borde del capture VARÍA por run (posición/tamaño de la ventana de WinUAE): un
// origen fijo desplazaba el muestreo 1 px cuando el borde cambiaba (fallos falsos de
// borde). Se DETECTA el origen del campo: primera columna/fila con un tramo largo
// no-negro (el campo mide 512 px de alto en el capture 2x); si no se detecta, se cae
// al valor histórico 39/15.
const S = 2;
// Origen del campo por el BLANCO IDENTIDAD del índice 31 (color (i<<6)|0x3F -> PNG
// (119,255,255)): solo los actores lo pintan, así que su bbox ancla la geometría —
// su borde izquierdo = el píxel 31 más a la IZQUIERDA del patrón (col 1, filas 6-8)
// del sub 0, y su borde superior = la fila 0 del arte (y=144). El borde del capture
// VARÍA por run (ventana), así que un OX/OY fijo desplazaba el muestreo.
function detectOriginFromWhite(png) {
	const W = png.width, H = png.height, d = png.data;
	let x0 = 1e9, y0 = 1e9, x1 = -1, y1 = -1;
	for (let y = 0; y < H; y++) {
		for (let x = 0; x < W; x++) {
			const i = (y * W + x) * 4;
			if (d[i] === 119 && d[i + 1] === 255 && d[i + 2] === 255) {
				x0 = Math.min(x0, x); x1 = Math.max(x1, x);
				y0 = Math.min(y0, y); y1 = Math.max(y1, y);
			}
		}
	}
	if (x1 < 0) return { bx: -1, by: -1 };
	return { bx: x0 - 1 * S, by: y0 - 144 * S };
}
const ACTOR_Y = 144;
const COLS = [0, 1, 2, 3, 4, 7, 8, 9, 11, 12, 13, 14, 15, 16, 17, 18];

// Patrón del actor: se extrae del fuente de la demo (única fuente de verdad).
function loadActorPixels() {
	const src = fs.readFileSync(
		path.join('demos', 'techniques', 'amiga', 'effects', '121_proc_corner_layers', 'src', 'main.cpp'),
		'utf8'
	);
	const m = src.match(/kActorPixels\[kTile \* kTile\] = \{([\s\S]*?)\};/);
	if (!m) throw new Error('no se encontro la tabla kActorPixels en la demo');
	const values = m[1].match(/\d+/g).map((v) => parseInt(v, 10));
	if (values.length !== 256) throw new Error(`kActorPixels con ${values.length} valores (esperados 256)`);
	return values;
}
const actor = loadActorPixels();

// Paleta identidad del modo golden: color = (i<<6)|0x3F; PNG = nibble*17.
function decodeIndex(r, g, b) {
	if (b !== 255 || r % 17 !== 0 || g % 17 !== 0) return -1;
	const r4 = r / 17, g4 = g / 17;
	if ((g4 & 3) !== 3) return -1;
	const idx = r4 * 4 + ((g4 & 0xC) >> 2);
	return idx < 32 ? idx : -1;
}

function load(file) {
	const png = PNG.sync.read(fs.readFileSync(file));
	return png;
}
const golden = load(goldenPath);
const pristine = load(pristinePath);
if (golden.width !== pristine.width || golden.height !== pristine.height) {
	console.error('capturas de tamanos distintos');
	process.exit(2);
}
// Origen del campo detectado en la captura GOLDEN (ver `detectOriginFromWhite`).
const { bx, by } = detectOriginFromWhite(golden);
const OX = bx >= 0 ? bx / S : 39;
const OY = by >= 0 ? by / S : 15;
console.log(`[origen] campo png (${bx},${by}) -> OX=${OX} OY=${OY}`);
function px(png, ax, ay) {
	const x = (ax + OX) * S, y = (ay + OY) * S;
	if (x < 0 || y < 0 || x >= png.width || y >= png.height) return { r: -1, g: -1, b: -1 };
	const i = (y * png.width + x) * 4;
	return { r: png.data[i], g: png.data[i + 1], b: png.data[i + 2] };
}
const sameRGB = (a, b) => a.r === b.r && a.g === b.g && a.b === b.b;

let fails = 0;
const report = { actor: [], window: 0, outside: 0, perSub: [] };
// Unión de los patrones de los 16 actores (para el chequeo de ventana solapada).
const coveredAll = new Set();
for (let k = 0; k < 16; k++) {
	const X = COLS[k] * 16;
	for (let r = 0; r < 16; r++) {
		for (let c = 0; c < 16; c++) {
			if (actor[r * 16 + c] !== 0) coveredAll.add((ACTOR_Y + r) * 4096 + (X + k + c));
		}
	}
}
for (let k = 0; k < 16; k++) {
	const X = COLS[k] * 16;
	let actorBad = 0, actorCount = 0, winBad = 0, firstBad = null;
	// (1) píxeles del actor, exactos.
	for (let r = 0; r < 16; r++) {
		for (let c = 0; c < 16; c++) {
			const v = actor[r * 16 + c];
			if (v === 0) continue;
			actorCount++;
			const g = px(golden, X + k + c, ACTOR_Y + r);
			const idx = decodeIndex(g.r, g.g, g.b);
			if (idx !== v) {
				actorBad++;
				if (!firstBad) firstBad = { x: X + k + c, y: ACTOR_Y + r, want: v, got: idx, rgb: g };
			}
		}
	}
	// (2) la ventana de 32 px del blit, fuera del actor: intacta (== prístino). Las
	// ventanas de actores VECINOS se solapan (cols adyacentes 0..4/7..9/11..16), así que
	// se excluye también la UNIÓN de los patrones de los 16 (esos píxeles son de otro
	// actor, no daño).
	const covered = new Set();
	for (let r = 0; r < 16; r++) {
		for (let c = 0; c < 16; c++) {
			if (actor[r * 16 + c] !== 0) covered.add((ACTOR_Y + r) * 4096 + (X + k + c));
		}
	}
	for (let r = 0; r < 16; r++) {
		for (let x = X; x < X + 32; x++) {
			const key = (ACTOR_Y + r) * 4096 + x;
			if (covered.has(key) || coveredAll.has(key)) continue;
			const a = px(golden, x, ACTOR_Y + r);
			const b = px(pristine, x, ACTOR_Y + r);
			if (!sameRGB(a, b)) winBad++;
		}
	}
	report.perSub.push({ sub: k, x: X + k, actorCount, actorBad, winBad });
	if (actorBad) { fails++; report.actor.push({ sub: k, actorBad, firstBad }); }
	if (winBad) fails++;
}

// (3) resto de la pantalla: idéntico (recorrido completo del campo, excluyendo ventanas).
const winTest = (ax, ay) => {
	if (ay < ACTOR_Y || ay >= ACTOR_Y + 16) return false;
	for (const col of COLS) {
		if (ax >= col * 16 && ax < col * 16 + 32) return true;
	}
	return false;
};
let outsideBad = 0, firstOutside = null;
for (let ay = 0; ay < 208; ay++) {
	for (let ax = 0; ax < 320; ax++) {
		if (winTest(ax, ay)) continue;
		const a = px(golden, ax, ay);
		const b = px(pristine, ax, ay);
		if (!sameRGB(a, b)) {
			outsideBad++;
			if (!firstOutside) firstOutside = { x: ax, y: ay, golden: a, pristine: b };
		}
	}
}
report.outside = outsideBad;
if (outsideBad) fails++;

if (jsonOut) {
	console.log(JSON.stringify({ ok: fails === 0, fails, outsideBad, firstOutside, report }, null, 1));
} else {
	for (const s of report.perSub) {
		const tag = s.actorBad === 0 && s.winBad === 0 ? 'OK ' : 'MAL';
		console.log(`[${tag}] sub ${String(s.sub).padStart(2)} x=${String(s.x).padStart(3)} ` +
			`actor=${s.actorCount}px fallos_actor=${s.actorBad} fallos_ventana=${s.winBad}`);
	}
	console.log(`[${outsideBad === 0 ? 'OK ' : 'MAL'}] fuera de ventanas: ${outsideBad} px distintos` +
		(firstOutside ? ` (primero: ${JSON.stringify(firstOutside)})` : ''));
	for (const a of report.actor) {
		console.log(`  sub ${a.sub}: primer fallo ${JSON.stringify(a.firstBad)}`);
	}
	console.log(fails === 0
		? 'GOLDEN OK: las 16 subposiciones del actor renderizan exactas, sin dano colateral.'
		: `GOLDEN FALLO: ${fails} comprobacion(es).`);
}
process.exit(fails === 0 ? 0 : 1);
