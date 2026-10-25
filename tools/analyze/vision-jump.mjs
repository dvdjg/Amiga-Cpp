#!/usr/bin/env node
// Comprobación semántica específica de salto sobre una secuencia de frames.
//
// Uso:
//   node tools/analyze/vision-jump.mjs <seqDir> [--model m] [--require]
//
// Envía los frames en orden temporal a Ollama y exige que el VLM observe un
// desplazamiento vertical del protagonista (subida y bajada), no solo cambios
// de contador/fps. `--require` convierte un NO en exit 1.

import fs from 'node:fs';
import path from 'node:path';
import { spawn } from 'node:child_process';

const BASE = 'http://127.0.0.1:11434';
const argv = process.argv.slice(2);
const positional = [];
let model = 'qwen3-vl:8b-instruct-q8_0';
let require = false;
for (let i = 0; i < argv.length; ++i) {
	if (argv[i] === '--model') model = argv[++i];
	else if (argv[i] === '--require') require = true;
	else positional.push(argv[i]);
}
const [seqDirArg] = positional;
if (!seqDirArg) {
	console.error('uso: node tools/analyze/vision-jump.mjs <seqDir> [--model m] [--require]');
	process.exit(2);
}
const seqDir = path.resolve(seqDirArg);
const files = fs.readdirSync(seqDir)
	.filter((f) => /^frame_\d+(_f\d+)?\.png$/.test(f))
	.sort();
if (files.length === 0) {
	console.error(`[vision-jump] no hay frames en ${seqDir}`);
	process.exit(3);
}

async function health() {
	try {
		const r = await fetch(`${BASE}/api/version`, { signal: AbortSignal.timeout(2000) });
		return r.ok ? await r.json() : null;
	} catch {
		return null;
	}
}
let ver = await health();
if (!ver) {
	const exe = [process.env.USERPROFILE + '\\AppData\\Local\\Programs\\Ollama\\ollama.exe',
		'C:\\Program Files\\Ollama\\ollama.exe'].find((p) => fs.existsSync(p));
	if (exe) {
		spawn(exe, ['serve'], { stdio: 'ignore', detached: true }).unref();
		for (let i = 0; i < 24 && !ver; ++i) {
			await new Promise((r) => setTimeout(r, 500));
			ver = await health();
		}
	}
}
if (!ver) {
	console.error('[vision-jump] Ollama no disponible');
	process.exit(2);
}

const images = files.map((f) => fs.readFileSync(path.join(seqDir, f)).toString('base64'));
const prompt = [
	'Las imagenes adjuntas van EN ORDEN TEMPORAL.',
	'El protagonista es el personaje naranja con forma humana y contorno claro situado en el',
	'centro de la pantalla; NO es ninguno de los enemigos (triangulos rojos/naranjas, objetos',
	'verdes, blancos o rosas) ni las columnas decorativas de la cueva.',
	'El suelo es la franja marron con borde verde de la parte inferior.',
	'Describe si ese personaje naranja realiza un SALTO: primero sube verticalmente respecto al',
	'suelo y despues baja.',
	'No confundas el cambio del contador de frames o de FPS con movimiento.',
	'Responde primero con una linea exacta "SALTO: SI" o "SALTO: NO", y despues justifica brevemente',
	'que cambia en la posicion vertical del personaje entre las imagenes.'
].join(' ');
const res = await fetch(`${BASE}/api/generate`, {
	method: 'POST',
	headers: { 'Content-Type': 'application/json' },
	body: JSON.stringify({ model, prompt, images, stream: false }),
	signal: AbortSignal.timeout(600000),
});
if (!res.ok) {
	console.error(`[vision-jump] HTTP ${res.status}`);
	process.exit(2);
}
const answer = (await res.json())?.response?.trim() ?? '';
console.log(answer);
const positive = /\bSALTO:\s*SI\b/i.test(answer);
if (require && !positive) process.exit(1);
