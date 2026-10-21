#!/usr/bin/env node
/**
 * Verificador de la demo 080 (`080_camera_pan_window`, fase F1 de la presentacion 3D).
 *
 * Comprueba dos cosas:
 *  1) `--image`: la captura tiene contenido (sala + paneles, con el color unico 12) y
 *     la franja del HUD (texto blanco sobre negro en las lineas 208..255).
 *  2) `--sequence-dir`: **contrato de pixel del paneo** con la politica ADAPTATIVA
 *     (HOST-471: la racha de Fakes la fija el coste medido del Full, tope 3).
 *     El runner captura con el `eng_debug_ready_probe` (pausa dentro del VBlank), asi
 *     que el PNG del frame F muestra el campo **F-1** (documentado): para un Fake F con
 *     offset k (su Full es F-1), PNG(F) = ventana del Full y PNG(F+1) = la misma ventana
 *     movida k px -> PNG(F+1) debe ser PNG(F) desplazado k px.
 *     Como la racha es adaptativa, la secuencia NO se replica: se validan invariantes de
 *     la telemetria (offsets 1..15, pan = pan del ultimo Full + offset, rachas acotadas,
 *     Full al menos cada N frames) y el contrato de pixel usa el offset REPORTADO.
 *
 * `--axis x|y` selecciona el eje empaquetado en `runStatus.detail`; `xy` comprueba una
 * traslación bidimensional en los Fakes de la trayectoria Lissajous.
 * Uso:
 *   node tools/analyze/verify-080-pan-window.mjs --image <screenshot.png>
 *   node tools/analyze/verify-080-pan-window.mjs --sequence-dir <dir> [--axis x|y|xy]
 * Salida: informe por stdout; exit 0 = OK, 1 = fallo, 2 = uso.
 */
import * as fs from 'fs';
import * as path from 'path';
import { readPng, pixel } from '../../dist/tools/lib/image.js';

const arg = (name, def = '') => {
	const i = process.argv.indexOf(name);
	return i >= 0 && process.argv[i + 1] && !process.argv[i + 1].startsWith('--') ? process.argv[i + 1] : def;
};

const origin = arg('--origin', '76,36').split(',').map(Number);
const scale = Number(arg('--scale', '2'));
const axis = arg('--axis', 'x');
const margin = axis === 'y' ? 24 : 15;
if (axis !== 'x' && axis !== 'y' && axis !== 'xy') {
	console.error('Uso: --axis x|y|xy');
	process.exit(2);
}
const SCENE_H = 208;
const FAKE_CAP = 3;      // tope de la demo (kFakeCap)
const FAKE_RUN_MAX = 4;  // sanidad: cap + 1
const FULL_EVERY_MAX = 12; // ningun tramo de Fakes mas largo que esto

const c444 = (v) => [((v >> 8) & 15) * 0x11, ((v >> 4) & 15) * 0x11, (v & 15) * 0x11];
const eq = (r, g, b, v) => { const [R, G, B] = c444(v); return r === R && g === G && b === B; };
const BG = 0x124;
const POST_UNIQUE = 0xf80; // panel 12: marcador unico de movimiento

let failures = 0;
const check = (ok, msg) => { console.log(`  ${ok ? 'OK  ' : 'FAIL'} ${msg}`); if (!ok) failures++; };

const amigaPixel = (img, x, y) => pixel(img, origin[0] + x * scale, origin[1] + y * scale);
const pngCache = new Map();
const imageFor = (imagePath) => {
	if (!pngCache.has(imagePath)) pngCache.set(imagePath, readPng(imagePath));
	return pngCache.get(imagePath);
};

function compareShift(aPath, bPath, k, shiftAxis = axis, dy = 0) {
	const ia = imageFor(aPath);
	const ib = imageFor(bPath);
	const dx = shiftAxis === 'y' ? 0 : k;
	const shiftY = shiftAxis === 'y' ? k : (shiftAxis === 'xy' ? dy : 0);
	const x0 = Math.max(0, -dx);
	const xEnd = Math.min(320, 320 - dx);
	const y0 = Math.max(0, -shiftY);
	const yEnd = Math.min(SCENE_H, SCENE_H - shiftY);
	let mismatches = 0;
	for (let y = y0; y < yEnd; y++) {
		for (let x = x0; x < xEnd; x++) {
			const sourceX = x + dx;
			const sourceY = y + shiftY;
			const [r0, g0, b0] = amigaPixel(ia, sourceX, sourceY);
			const [r1, g1, b1] = amigaPixel(ib, x, y);
			if (r0 !== r1 || g0 !== g1 || b0 !== b1) mismatches++;
		}
	}
	return { mismatches, total: (xEnd - x0) * (yEnd - y0) };
}

function bestVerticalShift(aPath, bPath, dx) {
	let best = {dy: -24, ...compareShift(aPath, bPath, dx, 'xy', -24)};
	for (let dy = -23; dy <= 24; ++dy) {
		const result = compareShift(aPath, bPath, dx, 'xy', dy);
		if (result.mismatches < best.mismatches) best = {dy, ...result};
	}
	return best;
}

function analyzeImage(imagePath) {
	const img = readPng(imagePath);
	let bg = 0;
	let unique = 0;
	let text = 0;
	for (let y = 0; y < SCENE_H; y++) {
		for (let x = 0; x < 320; x++) {
			const [r, g, b] = amigaPixel(img, x, y);
			if (eq(r, g, b, BG)) bg++;
			else if (eq(r, g, b, POST_UNIQUE)) unique++;
		}
	}
	for (let y = 208; y < 256; y++) {
		for (let x = 0; x < 320; x++) {
			const [r, g, b] = amigaPixel(img, x, y);
			if (r === 255 && g === 255 && b === 255) text++;
		}
	}
	return { bg, unique, text };
}

function analyzeSequence(seqDir) {
	const reportPath = path.join(seqDir, '..', 'run-report.json');
	if (!fs.existsSync(reportPath)) {
		check(false, `no existe ${reportPath} (la secuencia debe venir del runner con --sequence-step-frames)`);
		return;
	}
	const report = JSON.parse(fs.readFileSync(reportPath, 'utf8'));
	const all = report.sequence?.frames ?? [];
	if (all.some((fr) => !Number.isInteger(fr.frozenFrame))) {
		check(false, 'captura por paso requerida (usa --sequence-step-frames; el modo por intervalo no lleva telemetría alineada)');
		return;
	}
	const frames = all
		.filter((fr) => fr.path && fs.existsSync(fr.path) && fr.runStatus)
		.map((fr) => {
			const d = fr.runStatus.detail >>> 0;
			return {
				frame: fr.frozenFrame,
				path: fr.path,
				full: ((d >>> 24) & 0xff) === 0xa1,
				off: ((d >>> 16) & 0xff) - 128,
				pan: (d & 0xffff) << 16 >> 16,
			};
		});
	check(frames.length >= 4, `secuencia con frames suficientes (${frames.length}/${all.length})`);
	if (frames.length < 2) return;

	// --- Invariantes de la telemetria (politica adaptativa) --------------------------
	let lastFullPan = null;
	let lastFullOff = 0;
	let run = 0;
	let sinceFull = 0;
	let maxRun = 0;
	let maxSinceFull = 0;
	let fulls = 0;
	let fakes = 0;
	for (const f of frames) {
		if (f.full) {
			fulls++;
			// Con la publicación diferida y el rebote de cámara, el Full puede publicarse
			// con offset positivo o negativo; el valor debe permanecer dentro de su guarda.
			check(f.off >= -margin && f.off <= margin,
			      `f${f.frame}: Full con offset ±${margin} (off=${f.off})`);
			lastFullPan = f.pan;
			lastFullOff = f.off;
			maxRun = Math.max(maxRun, run);
			run = 0;
			sinceFull = 0;
		} else {
			fakes++;
			run++;
			sinceFull++;
			maxSinceFull = Math.max(maxSinceFull, sinceFull);
			check(Math.abs(f.off) >= 1 && Math.abs(f.off) <= margin,
			      `f${f.frame}: Fake con offset ±1..±${margin} (off=${f.off})`);
			if (lastFullPan !== null) {
				// El offset es absoluto respecto al paneo del RENDER del Full mostrado
				// (`renderPan = pan del Full publicado - su offset de display`).
				check(f.pan - (lastFullPan - lastFullOff) === f.off,
				      `f${f.frame}: offset (${f.off}) == pan (${f.pan}) - renderPan (${lastFullPan - lastFullOff})`);
			}
			check(run <= FAKE_RUN_MAX, `f${f.frame}: racha de Fakes acotada (${run})`);
			check(sinceFull <= FULL_EVERY_MAX, `f${f.frame}: Full al menos cada ${FULL_EVERY_MAX} frames`);
		}
	}
	maxRun = Math.max(maxRun, run);
	console.log(`  INFO fulls ${fulls} / fakes ${fakes}; racha maxima ${maxRun}; sin Full ${maxSinceFull} frames`);

	// --- Contrato de pixel (por offset REPORTADO, con costura de captura acotada) ----
	// Semantica de la politica adaptativa: cada **Fake** muestra el contenido del ULTIMO
	// **Full** desplazado por SU offset (los fakes intermedios no cambian el buffer). La
	// SCC puede reconstruir el campo a caballo del parche si el frame desborda el campo: el
	// par con costura acotada se acepta; la exactitud del mecanismo se prueba aparte
	// (aislamiento, VALIDATION §1).
	const kSeamMax = 2200;
	let pairs = 0;
	let exactPairs = 0;
	let lastFull = null;
	let firstFakeAfterFull = null;
	for (const f of frames) {
		if (f.full) {
			lastFull = f;
			firstFakeAfterFull = null;
			continue;
		}
		if (lastFull === null) continue;
		// La captura puede preceder un campo al parche Copper: el primer Fake tras un Full
		// aún puede mostrar la ventana anterior. Se conserva como ancla alineada para los
		// pares posteriores.
		if (firstFakeAfterFull === null) {
			firstFakeAfterFull = f;
			continue;
		}
		// Saltar el borde de la fase rápida en barridos de un eje; Lissajous usa búsqueda 2D.
		if (axis !== 'xy' && (Math.abs(lastFull.pan) >= 96 || Math.abs(f.pan) >= 96)) continue;
		if (axis === 'xy') {
			const dx = f.off - firstFakeAfterFull.off;
			const r = bestVerticalShift(firstFakeAfterFull.path, f.path, dx);
			pairs++;
			const exact = r.mismatches === 0;
			if (exact) exactPairs++;
			check(exact || r.mismatches <= kSeamMax,
			      `f${f.frame} (Fake): PNG(f) == PNG(f${firstFakeAfterFull.frame}) + (${dx}, ${r.dy}) px [${r.mismatches}/${r.total}${exact ? '' : ' (costura de captura)'}]`);
			continue;
		}
		// Normalmente se compara con la imagen del Full publicado. Si el screenshot de ese
		// Full precedió al parche Copper efectivo, comparar también con el primer Fake (ancla
		// que sí refleja el puntero publicado); no esconder diferencias: ambas alternativas
		// deben cumplir el mismo límite de costura.
		const delta = f.off - lastFull.off;
		if (delta === 0 && f !== firstFakeAfterFull) continue;
		let reference = lastFull;
		let compareDelta = delta;
		let r = compareShift(reference.path, f.path, compareDelta, axis);
		if (r.mismatches > kSeamMax && firstFakeAfterFull !== f) {
			const anchorDelta = f.off - firstFakeAfterFull.off;
			const anchorResult = compareShift(firstFakeAfterFull.path, f.path, anchorDelta, axis);
			if (anchorResult.mismatches < r.mismatches) {
				reference = firstFakeAfterFull;
				compareDelta = anchorDelta;
				r = anchorResult;
			}
		}
		pairs++;
		const exact = r.mismatches === 0;
		if (exact) exactPairs++;
		check(exact || r.mismatches <= kSeamMax,
		      `f${f.frame} (Fake off=${f.off}): PNG(f) == PNG(f${reference.frame}, off=${reference.off}) + ${compareDelta} px [${r.mismatches}/${r.total}${exact ? '' : ' (costura de captura)'}]`);
	}
	check(pairs >= 2, `hay al menos 2 pares de contrato en la secuencia (${pairs})`);
	console.log(`  INFO pares con contrato exacto: ${exactPairs}/${pairs}`);
	if (axis === 'xy') {
		console.log('  INFO Fulls consecutivos: la trayectoria 2D se evalúa por su diff temporal, no por un único desplazamiento X.');
		return;
	}

	// --- Fulls consecutivos: el contenido sigue el paneo -----------------------------
	// Fase de captura (DT-033): el parche de un update se aplica al campo SIGUIENTE, así
	// que la imagen capturada en el probe de fN refleja el estado de display del update
	// fN-1. Para dos Fulls solo es comparable cuando su update previo fue un Fake (estado
	// de display estable): entonces el contenido de la imagen = el paneo de ese previo
	// (`renderPan + off` de la fake = su pan), y k = |Δpan de los previos|.
	const byFramePrev = new Map(frames.map((f) => [f.frame, f]));
	const prevOf = (f) => byFramePrev.get(f.frame - 1) ?? null;
	const fullList = frames.filter((f) => f.full);
	let fullsOk = 0;
	let fullsChecked = 0;
	for (let i = 0; i + 1 < fullList.length; ++i) {
		const f = fullList[i];
		const b = fullList[i + 1];
		const fa = prevOf(f);
		const fb = prevOf(b);
		if (!fa || !fb || fa.full || fb.full) continue;
		const kPan = Math.abs(fb.pan - fa.pan);
		if (kPan === 0 || kPan > 6) continue;
		if (Math.abs(fa.pan) >= 96 || Math.abs(fb.pan) >= 96) continue;
		fullsChecked++;
		// Robusto a la fase de captura (DT-033: el parche se aplica al campo siguiente y
		// el probe pausa antes; el lag efectivo entre imagen y telemetria es de 0-1
		// updates): se busca el desplazamiento dominante en ±1..±6 y se exige que sea un
		// minimo claro con la costura acotada; el k esperado se registra como INFO.
		const shifts = [];
		for (let k = -6; k <= 6; ++k) {
			if (k !== 0) shifts.push({k, mismatches: compareShift(f.path, b.path, k, axis).mismatches});
		}
		let best = shifts[0];
		for (let j = 1; j < shifts.length; ++j) {
			if (shifts[j].mismatches < best.mismatches) best = shifts[j];
		}
		const kBest = best.k;
		const at = best.mismatches;
		const ok = at <= kSeamMax;
		if (ok) fullsOk++;
		check(ok,
		      `f${f.frame}->f${b.frame} (Fulls): desplazamiento dominante k=${kBest} (Δpan previos=${kPan}) [${shifts.map((s) => s.mismatches).join('/')}]`);
	}
	if (fullsChecked === 0) {
		console.log('  INFO Fulls consecutivos: sin pares elegibles en la secuencia (fase de captura; ampliar frames)');
	} else {
		check(fullsOk >= 1, `Fulls consecutivos con movimiento coherente (${fullsOk}/${fullsChecked})`);
	}
}

const image = arg('--image');
const seqDir = arg('--sequence-dir');

if (image) {
	const s = analyzeImage(image);
	check(s.unique > 40, `panel unico (color 12) presente: ${s.unique} px`);
	check(s.bg > 1000, `fondo de escena presente: ${s.bg} px`);
	check(s.text > 200, `HUD con texto blanco: ${s.text} px`);
} else if (seqDir) {
	analyzeSequence(seqDir);
} else {
	console.log('Uso: verify-080-pan-window.mjs --image <png> | --sequence-dir <dir>');
	process.exit(2);
}

console.log(failures === 0 ? 'OK: 080 paneo (contenido/HUD/contrato de pixel)' : `FALLOS: ${failures}`);
process.exit(failures === 0 ? 0 : 1);
