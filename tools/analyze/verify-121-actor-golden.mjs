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

// Geometría de la demo (field -> png 2x con borde de 38/15 px amiga).
const OX = 38, OY = 15, S = 2;
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
function px(png, ax, ay) {
	const x = (ax + OX) * S, y = (ay + OY) * S;
	if (x < 0 || y < 0 || x >= png.width || y >= png.height) return { r: -1, g: -1, b: -1 };
	const i = (y * png.width + x) * 4;
	return { r: png.data[i], g: png.data[i + 1], b: png.data[i + 2] };
}
const sameRGB = (a, b) => a.r === b.r && a.g === b.g && a.b === b.b;

let fails = 0;
const report = { actor: [], window: 0, outside: 0, perSub: [] };
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
	// (2) la ventana de 32 px del blit, fuera del actor: intacta (== prístino).
	const covered = new Set();
	for (let r = 0; r < 16; r++) {
		for (let c = 0; c < 16; c++) {
			if (actor[r * 16 + c] !== 0) covered.add((ACTOR_Y + r) * 4096 + (X + k + c));
		}
	}
	for (let r = 0; r < 16; r++) {
		for (let x = X; x < X + 32; x++) {
			if (covered.has((ACTOR_Y + r) * 4096 + x)) continue;
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
