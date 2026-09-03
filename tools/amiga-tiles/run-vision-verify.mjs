#!/usr/bin/env node
// run-vision-verify.mjs — verificación por IA de visión LOCAL (ollama) de
// out/tile-demos. Sin vision model instalado no hace nada (no inventa).
//
// Flujo automático (por demo), SIEMPRE que haya ollama+visión:
//   1. DESCRIBE la imagen ORIGEN y cada RESULTADO (reconstruct, tilebank,
//      bands_preview, source_resized).
//   2. OPERACIONES: pide al modelo qué hacer con el ORIGEN para preparar assets
//      de juego: recortar/extraer trozos si ve varios dibujos independientes
//      sobre un fondo común, proponer colores transparentes, paleta, remuestreo,
//      EHB, separación de planos… (guarda <origen>.ops.txt).
//   3. CORRESPONDENCIA: monta ORIGEN|RESULTADO lado a lado y pregunta si se
//      corresponden y qué diferencias ve (guarda <resultado>.compare.txt).
//
// Salidas: <imagen>.vision.txt, <origen>.ops.txt, <resultado>.compare.txt,
// <demo>/vision_report.md y VISION_SUMMARY.md.
// Flags: --model M, --resume, --folder DEMO, --all, --limit N.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { PNG } from 'pngjs';
import jpeg from 'jpeg-js';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(__dirname, '../..');
const DEMOS = path.join(ROOT, 'out', 'tile-demos');
const BASE = 'http://127.0.0.1:11434';

// Normaliza PNG o JPEG a {width,height,data:Buffer RGBA}.
function decodeImage(buf, name) {
	if (/\.jpe?g$/i.test(name)) { const d = jpeg.decode(buf, { useTArray: true, formatAsRGBA: true }); return { width: d.width, height: d.height, data: Buffer.from(d.data) }; }
	return PNG.sync.read(buf);
}
const VISION_RE = /qwen.*vl|gemma|llava|moondream|bakllava|minicpm.*v|phi.*vision/i;
const argV = (n, d) => { const i = process.argv.indexOf(n); return i >= 0 && i + 1 < process.argv.length ? process.argv[i + 1] : d; };
const MODEL = argV('--model', '');
const ALL = process.argv.includes('--all');
const FOLDER = argV('--folder', '');
const RESUME = process.argv.includes('--resume');
const LIMIT = Math.max(0, parseInt(argV('--limit', '0'), 10) || 0);

async function ask(pngBytes, prompt, model) {
	const b64 = pngBytes.toString('base64');
	const body = { model, temperature: 0.2, max_tokens: 320,
		messages: [{ role: 'user', content: [
			{ type: 'text', text: prompt },
			{ type: 'image_url', image_url: { url: `data:image/png;base64,${b64}` } },
		] }] };
	const r = await fetch(`${BASE}/v1/chat/completions`, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) });
	if (!r.ok) throw new Error(`ollama ${r.status}: ${await r.text()}`);
	const j = await r.json();
	return (j.choices?.[0]?.message?.content ?? JSON.stringify(j)).trim();
}

async function detectVisionModel() {
	try {
		const tags = await (await fetch(`${BASE}/api/tags`)).json();
		const names = (tags.models || []).map((m) => m.name);
		if (!names.length) return null;
		return names.find((n) => /qwen3?-vl/i.test(n)) || names.find((n) => VISION_RE.test(n)) || null;
	} catch { return null; }
}

function walkImgs(dir) {
	const out = [];
	const stack = [dir];
	while (stack.length) {
		const d = stack.pop();
		for (const e of fs.readdirSync(d, { withFileTypes: true })) {
			const p = path.join(d, e.name);
			if (e.isDirectory()) stack.push(p);
			else if (/\.(png|jpe?g)$/i.test(e.name)) out.push(path.relative(dir, p).split(path.sep).join('/'));
		}
	}
	return out;
}
function isSource(f) { return !f.includes('/') && !/^\./.test(f); }
function isDest(f) { return /(^|\/)(reconstruct|tilebank|bands_preview|source_resized)[_.]/.test(f); }

// Montaje ORIGEN | RESULTADO (escala §1 a máx 640 px de ancho cada lado; hueco 8 px).
function makePair(aName, aBytes, bBytes) {
	const A = decodeImage(aBytes, aName), B = PNG.sync.read(bBytes);
	const maxW = 640;
	const sc = (w) => Math.min(1, maxW / w);
	const wa = Math.max(8, Math.round(A.width * sc(A.width))), ha = Math.max(8, Math.round(A.height * sc(A.width)));
	const wb = Math.max(8, Math.round(B.width * sc(B.width))), hb = Math.max(8, Math.round(B.height * sc(B.width)));
	const H = Math.max(ha, hb);
	const W = wa + 8 + wb;
	const out = new PNG({ width: W, height: H });
	const blit = (src, dw, dh, odx, ody) => {
		for (let y = 0; y < dh; y++) for (let x = 0; x < dw; x++) {
			const sxo = Math.min(src.width - 1, Math.floor(x * src.width / dw));
			const syo = Math.min(src.height - 1, Math.floor(y * src.height / dh));
			const so = (syo * src.width + sxo) * 4, d = ((ody + y) * W + (odx + x)) * 4;
			out.data[d] = src.data[so]; out.data[d + 1] = src.data[so + 1]; out.data[d + 2] = src.data[so + 2]; out.data[d + 3] = 255;
		}
	};
	blit(A, wa, ha, 0, Math.floor((H - ha) / 2));
	blit(B, wb, hb, wa + 8, Math.floor((H - hb) / 2));
	return PNG.sync.write(out);
}

// Reutiliza <file> si no está vacío.
function cached(file) { if (RESUME && fs.existsSync(file)) { const t = fs.readFileSync(file, 'utf8').trim(); if (t) return t; } return null; }

function collectJobs() {
	const folders = fs.readdirSync(DEMOS).filter((n) => { try { return fs.statSync(path.join(DEMOS, n)).isDirectory(); } catch { return false; } }).sort();
	const sources = [];   // {folder, abs, rel}
	const results = [];   // {folder, abs, rel, kind}
	for (const folder of folders) {
		const base = path.join(DEMOS, folder);
		const files = walkImgs(base);
		for (const f of files) {
			if (isSource(f)) sources.push({ folder, abs: path.join(base, f), rel: `${folder}/${f}` });
			else if (isDest(f)) results.push({ folder, abs: path.join(base, f), rel: `${folder}/${f}`, kind: path.basename(f) });
		}
	}
	return { folders, sources, results };
}

async function main() {
	const model = MODEL || (await detectVisionModel());
	if (!model) { console.log('[vision] ollama local sin modelo de visión instalado. Nada que pedir.'); process.exit(0); }
	console.log(`[vision] ollama + modelo de visión -> ${model}`);
	if (!fs.existsSync(DEMOS)) { console.error(`no existe ${DEMOS}`); process.exit(1); }

	const all = collectJobs();
	const folders = FOLDER ? all.folders.filter((f) => f === FOLDER) : all.folders;
	const sources = all.sources.filter((s) => folders.includes(s.folder));
	const results = all.results.filter((r) => folders.includes(r.folder));
	if (LIMIT > 0) { /* limit aplica a resultados */ results.length = Math.min(results.length, LIMIT); }
	const DQ = 'Esta es la imagen ORIGEN de la demo. Describe contenido, estilo y paleta.';
	const RQ = 'Este es un RESULTADO de la demo (imagen cuantizada/tilebank/preview). Describe qué se ve y evalúa calidad/artefactos.';

	console.log(`[vision] describe: ${sources.length} orígenes + ${results.length} resultados`);
	let i = 0;
	const desc = new Map(); // rel -> txt
	async function describe(abs, rel, q, kind) {
		const f = `${abs}.vision.txt`;
		process.stdout.write(`  describe ${rel} … `);
		const prev = cached(f);
		if (prev !== null) { desc.set(rel, prev); console.log('reusado'); return; }
		try { const t = await ask(fs.readFileSync(abs), q, model); fs.writeFileSync(f, t + '\n', 'utf8'); desc.set(rel, t); console.log('OK'); }
		catch (e) { console.log(`ERROR ${e.message}`); }
	}
	for (const s of sources) await describe(s.abs, s.rel, DQ);
	for (const r of results) await describe(r.abs, r.rel, RQ);

	// 2) OPERACIONES sobre cada origen
	console.log(`[vision] operaciones: ${sources.length} orígenes`);
	const ops = new Map();
	for (const s of sources) {
		const f = `${s.abs}.ops.txt`;
		process.stdout.write(`  ops ${s.rel} … `);
		const prev = cached(f);
		const OPQ = 'Eres un preparador de ASSETS DE JUEGO. Sobre esta IMAGEN ORIGEN: 1) si ves varios dibujos/tiles independientes sobre un fondo común, propón RECORTAR y extraer cada trozo (da coordenadas aproximadas en %); 2) propón el color que podrías usar como TRANSPARENTE (si lo hay); 3) propón otras operaciones útiles para un juego Amiga (paleta/EHB, remuestreo, tamaño de tile, separación de planos, offsets). Respuesta breve y concreta.';
		if (prev !== null) { ops.set(s.rel, prev); console.log('reusado'); continue; }
		try { const t = await ask(fs.readFileSync(s.abs), OPQ, model); fs.writeFileSync(f, t + '\n', 'utf8'); ops.set(s.rel, t); console.log('OK'); }
		catch (e) { console.log(`ERROR ${e.message}`); }
	}

	// 3) CORRESPONDENCIA origen|resultado (solo cuando la demo tiene UN único origen)
	console.log(`[vision] comparación origen|resultado`);
	const compare = new Map();
	for (const r of results) {
		if (!/reconstruct\.png$/.test(r.rel)) continue;
		const src = sources.filter((s) => s.folder === r.folder);
		if (src.length !== 1) { console.log(`  (skip compare ${r.rel}: ${src.length} orígenes en la carpeta)`); continue; }
		const f = `${r.abs}.compare.txt`;
		process.stdout.write(`  compare ${r.rel} … `);
		const prev = cached(f);
		const CQ = 'Imagen montada: IZQUIERDA = ORIGEN, DERECHA = RESULTADO de cuantizar. ¿Se corresponden las dos mitades? ¿Qué diferencias/artefactos ves (banding, ruido, pérdida de detalle, desplazamientos)? Responde en 5 líneas máximo.';
		if (prev !== null) { compare.set(r.rel, prev); console.log('reusado'); continue; }
		try { const pair = makePair(src[0].rel, fs.readFileSync(src[0].abs), fs.readFileSync(r.abs)); const t = await ask(pair, CQ, model); fs.writeFileSync(f, t + '\n', 'utf8'); compare.set(r.rel, t); console.log('OK'); }
		catch (e) { console.log(`ERROR ${e.message}`); }
	}

	// Informes
	const byFolder = new Map();
	for (const folder of [...new Set([...sources, ...results].map((x) => x.folder))]) {
		const lines = [`# Evaluación por IA (${model}) — ${folder}`, ''];
		const add = (title, txt) => { if (txt) lines.push(`## ${title}`, '', txt, ''); };
		for (const s of sources.filter((x) => x.folder === folder)) {
			add(`${s.rel} (ORIGEN)`, desc.get(s.rel));
			add(`${s.rel} → OPERACIONES`, ops.get(s.rel));
		}
		for (const r of results.filter((x) => x.folder === folder)) {
			add(`${r.rel} (RESULTADO)`, desc.get(r.rel));
			add(`${r.rel} → CORRESPONDENCIA`, compare.get(r.rel));
		}
		fs.writeFileSync(path.join(DEMOS, folder, 'vision_report.md'), lines.join('\n'), 'utf8');
		byFolder.set(folder, lines);
	}
	const summary = [`# Resumen IA de visión — out/tile-demos`, '', `Modelo: ${model} · fecha 2026-09-04`, '', `Orígenes+resultados: ${sources.length}+${results.length} · ops: ${ops.size} · comparaciones: ${compare.size}`, ''];
	// VISION_SUMMARY global SIEMPRE se reconstruye desde los informes por carpeta.
	const foldersAll = fs.readdirSync(DEMOS).filter((n) => { try { return fs.statSync(path.join(DEMOS, n)).isDirectory() && !isNaN(parseInt(n, 10)); } catch { return false; } }).sort();
	for (const folder of foldersAll) {
		const mdPath = path.join(DEMOS, folder, 'vision_report.md');
		if (!fs.existsSync(mdPath)) continue;
		const body = fs.readFileSync(mdPath, 'utf8').replace(/^# .*\r?\n/, '').trim();
		summary.push(`# ${folder}`, '', body, '');
	}
	fs.writeFileSync(path.join(DEMOS, 'VISION_SUMMARY.md'), summary.join('\n'), 'utf8');
	console.log(`[vision] informes -> ${DEMOS}`);
}

main().catch((e) => { console.error(e); process.exit(1); });