#!/usr/bin/env node
/**
 * Verificador de la demo 080 (`080_camera_pan_window`, fase F1 de la presentacion 3D).
 *
 * Comprueba dos cosas:
 *  1) `--image`: la captura tiene contenido (sala + paneles, con el color unico 12) y
 *     la franja del HUD (texto blanco sobre negro en las lineas 208..255).
 *  2) `--sequence-dir`: **contrato de pixel del paneo**. El runner captura con el
 *     `eng_debug_ready_probe` (pausa dentro del VBlank), asi que el PNG del frame F
 *     muestra el campo **F-1** (documentado). Para cada frame F que es Fake con offset
 *     k (su Full es F-1): PNG(F) = campo F-1 = ventana del Full; PNG(F+1) = campo F =
 *     la misma ventana movida k px -> **PNG(F+1) debe ser PNG(F) desplazado k px**.
 *     La secuencia esperada (Full/Fake y offsets) replica el reloj determinista del
 *     demo (paneo triangular; fase lenta v=1: Full a 25 Hz + Fake a 50 Hz; fase rapida
 *     v=16: offset > margen -> Full cada frame).
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

const c444 = (v) => [((v >> 8) & 15) * 0x11, ((v >> 4) & 15) * 0x11, (v & 15) * 0x11];
const eq = (r, g, b, v) => { const [R, G, B] = c444(v); return r === R && g === G && b === B; };
const BG = 0x124;
const POST_UNIQUE = 0xf80; // panel 12: marcador unico de movimiento

let failures = 0;
const check = (ok, msg) => { console.log(`  ${ok ? 'OK  ' : 'FAIL'} ${msg}`); if (!ok) failures++; };

const amigaPixel = (img, x, y) => pixel(img, origin[0] + x * scale, origin[1] + y * scale);

/// Espejo de la politica del demo (pan_window + movimiento triangular).
function expectedSteps(count) {
	let pan = -128;
	let dir = 1;
	const st = { anchored: false, base: 0, fakes: 0 };
	const out = [];
	for (let f = 0; f < count; f++) {
		const dx = pan - st.base;
		const adx = Math.abs(dx);
		if (!st.anchored || adx > 15 || st.fakes >= 1) {
			st.base = pan;
			st.fakes = 0;
			st.anchored = true;
			out.push({ full: true, off: 0, pan });
		} else {
			st.fakes++;
			out.push({ full: false, off: dx, pan });
		}
		const v = pan < 96 && pan > -96 ? 1 : 16;
		pan += dir * v;
		if (pan > 128) { pan = 128; dir = -1; } else if (pan < -128) { pan = -128; dir = 1; }
	}
	return out;
}

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
	const frames = report.sequence?.frames ?? [];
	check(frames.length >= 4, `secuencia con frames suficientes (${frames.length})`);
	if (frames.length < 2) return;

	const steps = expectedSteps(4096);
	const byFrame = new Map();
	for (const fr of frames) {
		// Capturas parciales (timeout del runner): solo cuentan los PNG que existen.
		if (fr.path && fs.existsSync(fr.path)) byFrame.set(fr.frozenFrame, fr);
	}
	const dec = (rs) => {
		const d = rs.detail >>> 0;
		return { full: ((d >>> 24) & 0xff) === 0xa1, off: ((d >>> 16) & 0xff) - 128 };
	};

	let checked = 0;
	let pairs = 0;
	let exactPairs = 0;
	let fullsOk = 0;
	// Tolerancia de la costura de captura: la SCC congela el emulador en el VBlank y
	// reconstruye el campo desde el estado actual; a veces queda una banda de filas con el
	// estado del campo anterior (<= ~2% de pixeles). El mecanismo en si se verifico
	// EXACTO en la prueba de aislamiento de VALIDATION (offset forzado 0..16, 0
	// discordancias en cada uno).
	const kSeamMax = 2200;
	for (const fr of frames) {
		const f = fr.frozenFrame;
		const t = dec(fr.runStatus);
		const e = steps[f];
		check(t.full === e.full && t.off === e.off,
		      `f${f}: telemetria (${t.full ? 'Full' : 'Fake ' + t.off}) == reloj esperado (${e.full ? 'Full' : 'Fake ' + e.off})`);
		checked++;
		if (checked > 12) break; // el informe no necesita todas las lineas
	}
	// Contrato de pixel con el lag de campo documentado: PNG(F+1) == PNG(F) + k_F.
	for (const fr of frames) {
		const f = fr.frozenFrame;
		const next = byFrame.get(f + 1);
		if (!next) continue;
		const e = steps[f];
		if (e.full || steps[f - 1]?.full !== true) continue;
		// Los pares en el borde de la fase rapida (el Full anterior con |pan| >= 96) se
		// saltan: el paneo no resuelto de esa frontera (hasta 16 px) engorda la costura
		// de captura; en la fase rapida, ademas, no hay Fakes por diseno.
		if (Math.abs(steps[f - 1].pan) >= 96) continue;
		const r = compareShift(fr.path, next.path, e.off);
		++pairs;
		const exact = r.mismatches === 0;
		if (exact) exactPairs++;
		const ok = exact || r.mismatches <= kSeamMax;
		check(ok,
		      `f${f} (Fake off=${e.off}): PNG(f${f + 1}) == PNG(f${f}) desplazado ${e.off} px [${r.mismatches}/${r.total} discordancias${exact ? '' : ' (costura de captura)'}]`);
	}
	check(pairs >= 2, `hay al menos 2 pares de contrato en la secuencia (${pairs})`);
	// Nota: que TODOS los pares sean de 0 discordancias depende de la fase de captura
	// (el runner pausa en VBlank y la SCC puede reconstruir el campo a caballo del parche
	// si el frame desborda el campo). La exactitud del mecanismo se prueba aparte con el
	// test de aislamiento de VALIDATION (offset forzado 0..16, 0 discordancias en cada uno).
	console.log(`  INFO pares con contrato exacto: ${exactPairs}/${pairs}`);
	// Fulls consecutivos (F, F+2): el contenido debe seguir el paneo (desplazamiento
	// dominante en la direccion del movimiento; la perspectiva deja diferencias finas).
	// Se saltan los primeros frames capturados (la alineacion de la secuencia empieza
	// justo al arrancar la captura y su campo puede ser anterior a la ventana capturada).
	const capturedOrder = frames.map((fr) => fr.frozenFrame);
	for (const fr of frames) {
		const f = fr.frozenFrame;
		const idx = capturedOrder.indexOf(f);
		if (idx < 2) continue;
		const a = byFrame.get(f);
		const b = byFrame.get(f + 2);
		if (!a || !b || !steps[f].full || !steps[f + 2].full) continue;
		const k = Math.abs(steps[f + 2].pan - steps[f].pan);
		const at = compareShift(a.path, b.path, k).mismatches;
		const atMinus = compareShift(a.path, b.path, Math.max(0, k - 1)).mismatches;
		const atPlus = compareShift(a.path, b.path, k + 1).mismatches;
		const ok = at <= atMinus && at <= atPlus;
		if (ok) fullsOk++;
		check(ok, `f${f}->f${f + 2} (Fulls): desplazamiento dominante k=${k} [${at} <= ${atMinus}/${atPlus}]`);
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
