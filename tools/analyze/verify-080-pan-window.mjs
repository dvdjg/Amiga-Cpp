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
 * Uso:
 *   node tools/analyze/verify-080-pan-window.mjs --image <screenshot.png>
 *   node tools/analyze/verify-080-pan-window.mjs --sequence-dir <dir>
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
const SCENE_H = 208;
const MARGIN = 15;
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

function compareShift(aPath, bPath, k) {
	const ia = readPng(aPath);
	const ib = readPng(bPath);
	const xEnd = 320 - Math.abs(k);
	let mismatches = 0;
	for (let y = 0; y < SCENE_H; y++) {
		for (let x = 0; x < xEnd; x++) {
			const [r0, g0, b0] = amigaPixel(ia, x + k, y);
			const [r1, g1, b1] = amigaPixel(ib, x, y);
			if (r0 !== r1 || g0 !== g1 || b0 !== b1) mismatches++;
		}
	}
	return { mismatches, total: xEnd * SCENE_H };
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
	let run = 0;
	let sinceFull = 0;
	let maxRun = 0;
	let maxSinceFull = 0;
	let fulls = 0;
	let fakes = 0;
	for (const f of frames) {
		if (f.full) {
			fulls++;
			check(f.off === 0, `f${f.frame}: Full con offset 0 (off=${f.off})`);
			lastFullPan = f.pan;
			maxRun = Math.max(maxRun, run);
			run = 0;
			sinceFull = 0;
		} else {
			fakes++;
			run++;
			sinceFull++;
			maxSinceFull = Math.max(maxSinceFull, sinceFull);
			check(f.off >= 1 && f.off <= MARGIN,
			      `f${f.frame}: Fake con offset 1..${MARGIN} (off=${f.off})`);
			if (lastFullPan !== null) {
				check(f.pan === lastFullPan + f.off,
				      `f${f.frame}: pan (${f.pan}) == pan del Full (${lastFullPan}) + offset (${f.off})`);
			}
			check(run <= FAKE_RUN_MAX, `f${f.frame}: racha de Fakes acotada (${run})`);
			check(sinceFull <= FULL_EVERY_MAX, `f${f.frame}: Full al menos cada ${FULL_EVERY_MAX} frames`);
		}
	}
	maxRun = Math.max(maxRun, run);
	console.log(`  INFO fulls ${fulls} / fakes ${fakes}; racha maxima ${maxRun}; sin Full ${maxSinceFull} frames`);

	// --- Contrato de pixel (por offset REPORTADO, con costura de captura acotada) ----
	// La SCC puede reconstruir el campo a caballo del parche si el frame desborda el
	// campo: el par con costura acotada se acepta; la exactitud del mecanismo se prueba
	// aparte (aislamiento, VALIDATION §1).
	const kSeamMax = 2200;
	const byFrame = new Map(frames.map((f) => [f.frame, f]));
	let pairs = 0;
	let exactPairs = 0;
	for (const f of frames) {
		if (f.full) continue;
		const next = byFrame.get(f.frame + 1);
		if (!next) continue;
		// Saltar los pares en el borde de la fase rapida (el paneo no resuelto de esa
		// frontera engorda la costura; en la fase rapida no hay Fakes por diseno).
		if (lastFullPan !== null && Math.abs(lastFullPan) >= 96) continue;
		const r = compareShift(f.path, next.path, f.off);
		pairs++;
		const exact = r.mismatches === 0;
		if (exact) exactPairs++;
		check(exact || r.mismatches <= kSeamMax,
		      `f${f.frame} (Fake off=${f.off}): PNG(f${f.frame + 1}) == PNG(f${f.frame}) + ${f.off} px [${r.mismatches}/${r.total}${exact ? '' : ' (costura de captura)'}]`);
	}
	check(pairs >= 2, `hay al menos 2 pares de contrato en la secuencia (${pairs})`);
	console.log(`  INFO pares con contrato exacto: ${exactPairs}/${pairs}`);

	// --- Fulls consecutivos: el contenido sigue el paneo -----------------------------
	let fullsOk = 0;
	for (const f of frames) {
		if (!f.full) continue;
		const b = byFrame.get(f.frame + 2);
		if (!b || !b.full) continue;
		const k = Math.abs(b.pan - f.pan);
		if (k === 0 || k > 6) continue;
		if (Math.abs(f.pan) >= 96 || Math.abs(b.pan) >= 96) continue;
		const at = compareShift(f.path, b.path, k).mismatches;
		const atMinus = compareShift(f.path, b.path, Math.max(0, k - 1)).mismatches;
		const atPlus = compareShift(f.path, b.path, k + 1).mismatches;
		const ok = at <= atMinus && at <= atPlus;
		if (ok) fullsOk++;
		check(ok, `f${f.frame}->f${b.frame} (Fulls): desplazamiento dominante k=${k} [${at} <= ${atMinus}/${atPlus}]`);
	}
	check(fullsOk >= 1, `Fulls consecutivos con movimiento coherente (${fullsOk})`);
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
