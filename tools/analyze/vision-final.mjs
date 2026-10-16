#!/usr/bin/env node
// ---------------------------------------------------------------------------
// **Evaluación de visión FINAL de una demo** → `<demoDir>/src/VISION.md`.
//
// Regla del procedimiento (gate `tools/check/demo-docs.mjs`): la evaluación final
// usa SIEMPRE el **prompt canónico** (`tools/analyze/vision-prompt.txt`), idéntico
// para todas las demos y **sin pistas** de qué debe describir. Las preguntas
// concretas por entidad son para DEPURAR y no entran aquí (irán al informe crudo
// de `vision-run.mjs`).
//
// Uso:
//   node tools/analyze/vision-final.mjs <demoId> <dirSeq> [--model m]
//        [--demo-dir ruta] [--frames k]      (k = máximo de frames, default 5)
//
// Selecciona hasta `k` frames repartidos de la secuencia (primero…último), hace
// UNA llamada con todas las imágenes en orden temporal y escribe VISION.md con:
// metadatos (fecha, modelo, frames), el prompt canónico VERBATIM y la respuesta
// cruda. El checker verifica el marcador, el prompt exacto y que haya respuesta.
// ---------------------------------------------------------------------------
import fs from 'node:fs';
import path from 'node:path';
import { spawn } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const CANONICAL = fs.readFileSync(path.join(ROOT, 'tools/analyze/vision-prompt.txt'), 'utf8').trim();

const argv = process.argv.slice(2);
const positional = [];
let model = 'qwen3-vl:8b-instruct-q8_0';
let demoDirArg = '';
let maxFrames = 5;
for (let i = 0; i < argv.length; ++i) {
	const a = argv[i];
	if (a === '--model') model = argv[++i];
	else if (a === '--demo-dir') demoDirArg = argv[++i];
	else if (a === '--frames') maxFrames = Math.max(1, parseInt(argv[++i], 10));
	else positional.push(a);
}
const [demoId, seqDir] = positional;
if (!demoId || !seqDir) {
	console.error('uso: vision-final.mjs <demoId> <dirSeq> [--model m] [--demo-dir ruta] [--frames k]');
	process.exit(2);
}

let demoDir = demoDirArg;
if (!demoDir) {
	const hit = fs.readdirSync(path.join(ROOT, 'demos'), { recursive: true, withFileTypes: true })
		.find((e) => e.isDirectory() && e.name === demoId);
	if (!hit) { console.error(`[vision-final] no encuentro demos/**/${demoId}; pasa --demo-dir`); process.exit(3); }
	demoDir = path.join(hit.parentPath ?? hit.path, hit.name);
}
if (!path.isAbsolute(demoDir)) demoDir = path.join(ROOT, demoDir);

// ---- Frames: hasta `maxFrames`, repartidos (primero…último). ----
const files = fs.readdirSync(path.join(ROOT, seqDir)).filter((f) => /^frame_\d+(_f\d+)?\.png$/.test(f)).sort();
if (files.length === 0) { console.error('[vision-final] la secuencia no tiene frames'); process.exit(3); }
const pick = [];
if (files.length <= maxFrames) pick.push(...files);
else {
	for (let i = 0; i < maxFrames; ++i) {
		pick.push(files[Math.round((i * (files.length - 1)) / (maxFrames - 1))]);
	}
}

// ---- Ollama (mismo patrón que vision-run.mjs). ----
const BASE = 'http://127.0.0.1:11434';
async function health() {
	try { const r = await fetch(`${BASE}/api/version`, { signal: AbortSignal.timeout(2000) }); return r.ok ? await r.json() : null; }
	catch { return null; }
}
let ver = await health();
if (!ver) {
	const exe = [process.env.USERPROFILE + '\\AppData\\Local\\Programs\\Ollama\\ollama.exe',
		'C:\\Program Files\\Ollama\\ollama.exe'].find((c) => fs.existsSync(c));
	if (exe) {
		spawn(exe, ['serve'], { stdio: 'ignore', detached: true }).unref();
		for (let i = 0; i < 24 && !ver; i++) { await new Promise((r) => setTimeout(r, 500)); ver = await health(); }
	}
}
if (!ver) { console.error('[vision-final] Ollama no disponible'); process.exit(2); }
console.log(`[vision-final] Ollama ${ver.version}; frames: ${pick.join(', ')}`);

const images = pick.map((f) => fs.readFileSync(path.join(ROOT, seqDir, f)).toString('base64'));
const body = {
	model,
	stream: false,
	options: { temperature: 0 },
	messages: [{ role: 'user', content: CANONICAL, images }],
};
const res = await fetch(`${BASE}/api/chat`, {
	method: 'POST',
	headers: { 'content-type': 'application/json' },
	body: JSON.stringify(body),
	signal: AbortSignal.timeout(600000),
});
if (!res.ok) { console.error(`[vision-final] HTTP ${res.status}`); process.exit(2); }
const answer = (await res.json())?.message?.content?.trim() ?? '';

const out = [
	`# EVALUACIÓN DE VISIÓN — ${demoId}`,
	'',
	'<!-- generado por tools/analyze/vision-final.mjs; no editar a mano -->',
	'',
	`- Fecha: ${new Date().toISOString()}`,
	`- Modelo: \`${model}\``,
	`- Frames: ${pick.join(', ')} (${pick.length})`,
	`- Secuencia: \`${seqDir}\``,
	'',
	'## Prompt (canónico, tools/analyze/vision-prompt.txt)',
	'',
	'```text',
	CANONICAL,
	'```',
	'',
	'## Respuesta (cruda)',
	'',
	answer,
	'',
].join('\n');

const dest = path.join(demoDir, 'src', 'VISION.md');
fs.mkdirSync(path.dirname(dest), { recursive: true });
fs.writeFileSync(dest, out);
console.log(`[vision-final] escrito ${path.relative(ROOT, dest)} (${answer.length} chars)`);
