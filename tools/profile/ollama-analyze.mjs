#!/usr/bin/env node
/**
 * ollama-analyze.mjs — Analiza un perfil de WinUAE-DBG con Ollama local.
 *
 * Consume SOLO tokens del modelo local (sin nube). Dos tipos de analisis:
 *
 *  1) Vision (frames de pantalla): analiza los frames extraidos por
 *     profile-extract.mjs, bien frame a frame, bien como hoja de contacto
 *     (montaje) para ver la secuencia en una sola llamada.
 *  2) Meta (resto del perfil): envia profile-summary.json (registros custom,
 *     DMA, bitplanes, ciclos) a un modelo de texto local para un pre-analisis.
 *
 * El prompt BASE es generico y se puede personalizar por test con --prompt o
 * --prompt-file.
 *
 * Uso:
 *   node tools/profile/ollama-analyze.mjs <outDir> \
 *       [--model MODEL] [--base URL] [--mode frames|montage|meta|all] \
 *       [--frames a,b,c] [--prompt "texto"] [--prompt-file FILE] [--out FILE]
 */
import * as fs from 'fs';
import * as path from 'path';
import { execFileSync } from 'child_process';

function argValue(name, fallback) {
	const i = process.argv.indexOf(name);
	return i >= 0 && i + 1 < process.argv.length ? process.argv[i + 1] : fallback;
}
function usage() {
	console.error('Uso: ollama-analyze.mjs <outDir> [--model M] [--base URL] [--mode frames|montage|meta|all] [--frames a,b,c] [--prompt TEXTO] [--prompt-file FILE] [--out FILE]');
	process.exit(2);
}

// Prompt generico: sin asunciones sobre el chipset ni el contenido. Se puede
// reemplazar/ampliar por test con --prompt / --prompt-file.
const DEFAULT_PROMPT = `Eres un analizador de capturas de pantalla de una demo/programa para Amiga (OCS/ECS/AGA). Describe con PRECISION lo que se ve: colores, formas, elementos (bandas, blobs, tiles, sprites, texto), posiciones aproximadas (izquierda/centro/derecha, arriba/medio/abajo), y cualquier anomalia (negro interno, tearing, parpadeo, bandas incorrectas, corrupcion). Si analizas una secuencia, indica que cambia entre frames consecutivos. Responde breve y concreto (max 140 palabras).`;

const META_PROMPT = `Analiza este perfil de ejecucion de una demo Amiga y da un pre-analisis tecnico. Reporta: modo de pantalla (BPLCON0/1), ventana visible (DIWSTRT/STOP, DDFSTRT/STOP), punteros de bitplane y dimensiones, si la paleta/colores parecen coherentes, actividad DMA por tipo (blitter, copper, bitplane, sprite), ciclos de perfil vs idle, y cualquier registro sospechoso. Concluye si el frame parece correcto o si hay indicios de problema. Breve (max 180 palabras).`;

async function main() {
	const outDir = process.argv[2];
	if (!outDir) usage();
	const model = argValue('--model', process.env.OLLAMA_MODEL || 'qwen3-vl:8b-instruct-q8_0');
	const textModel = argValue('--text-model', process.env.OLLAMA_TEXT_MODEL || 'qwen3:8b');
	const base = argValue('--base', process.env.OLLAMA_BASE || 'http://127.0.0.1:11434');
	const mode = argValue('--mode', 'all');
	const framesArg = argValue('--frames', null);
	const promptExtra = argValue('--prompt', '');
	const promptFile = argValue('--prompt-file', null);
	const outFile = argValue('--out', path.join(outDir, 'ollama-report.md'));

	let basePrompt = DEFAULT_PROMPT;
	if (promptFile) {
		basePrompt = fs.readFileSync(promptFile, 'utf8').trim();
	} else if (promptExtra) {
		basePrompt = `${DEFAULT_PROMPT}\nRequisito extra del test: ${promptExtra}`;
	}

	const summaryPath = path.join(outDir, 'profile-summary.json');
	if (!fs.existsSync(summaryPath)) {
		console.error('No existe ' + summaryPath + '. Ejecuta primero profile-extract.mjs');
		process.exit(1);
	}
	const summary = JSON.parse(fs.readFileSync(summaryPath, 'utf8'));

	async function chat(modelName, messages) {
		const res = await fetch(`${base}/api/chat`, {
			method: 'POST', headers: { 'content-type': 'application/json' },
			body: JSON.stringify({ model: modelName, messages, stream: false })
		});
		if (!res.ok) throw new Error(`Ollama HTTP ${res.status}: ${await res.text()}`);
		const data = await res.json();
		return data.message?.content ?? '';
	}

	const report = [`# Análisis de perfil (visión ${model}, texto ${textModel})`, '', `Fuente: ${summary.file} · ${summary.numFrames} frame(s)`, ''];

	// ---- Modo meta: pre-análisis de TODO el perfil (texto, sin imagenes) ----
	if (mode === 'all' || mode === 'meta') {
		const metaText = JSON.stringify({ sectionBases: summary.sectionBases, frames: summary.frames.map((f) => ({ frame: f.frame, registers: f.registers, bitplanes: f.bitplanes, paletteEntries: f.palette?.map((p) => p.numEntries), dma: f.dma, profileCycles: f.profileCycles, idleCycles: f.idleCycles })) }, null, 1);
		console.log(`[ollama] pre-análisis del perfil (${textModel})...`);
		let meta;
		try { meta = await chat(textModel, [{ role: 'user', content: `${META_PROMPT}\n\n${metaText}` }]); }
		catch (e) { meta = `(error análisis meta: ${e.message})`; }
		report.push('## Pre-análisis del perfil (sin imágenes)', '', meta, '');
	}

	// ---- Modo vision: frames ----
	const frameIndices = summary.frames.map((f) => f.frame);
	let selected = frameIndices;
	if (framesArg) selected = framesArg.split(',').map((s) => parseInt(s.trim(), 10));

	if (mode === 'all' || mode === 'frames' || mode === 'montage') {
		const imgs = selected
			.map((idx) => summary.frames.find((f) => f.frame === idx))
			.filter((m) => m && fs.existsSync(path.join(outDir, m.screenshot)))
			.map((m) => path.join(outDir, m.screenshot));
		if (imgs.length === 0) {
			console.error('[ollama] no hay frames para analizar');
		} else if (mode === 'frames' || mode === 'all') {
			// Frame a frame, con contexto del anterior (fiable en cualquier modelo).
			report.push('## Frames (análisis individual)', '');
			let prev = '';
			for (const img of imgs) {
				const idx = path.basename(img);
				const meta = summary.frames.find((f) => f.screenshot === idx);
				const ctx = [
					`Frame ${meta ? meta.frame : idx}.`,
					meta?.registers?.BPLCON0 != null ? `BPLCON0=${meta.registers.BPLCON0} BPLCON1=${meta.registers.BPLCON1} DIW=${meta.registers.DIWSTRT}-${meta.registers.DIWSTOP} DDF=${meta.registers.DDFSTRT}-${meta.registers.DDFSTOP}` : '',
					meta?.bitplanes?.length ? `Bitplanes: ${meta.bitplanes.map((b) => '0x' + b.address.toString(16) + ' ' + b.width + 'x' + b.height + 'x' + b.numPlanes).join(', ')}` : '',
					prev ? `Contexto frame anterior: ${prev}` : ''
				].filter(Boolean).join('\n');
				const text = await chat(model, [{ role: 'user', content: `${basePrompt}\n${ctx}`, images: [fs.readFileSync(img).toString('base64')] }]);
				prev = text;
				report.push(`### ${idx}`, `![frame](${path.basename(img)})`, text, '');
				process.stdout.write(`[ollama] frame ${idx} analizado\n`);
			}
		}
		if (mode === 'montage' || mode === 'all') {
			// Hoja de contacto via ffmpeg (una sola imagen -> 1 llamada, fiable).
			report.push('## Secuencia (hoja de contacto)', '');
			const montagePath = path.join(outDir, '_montage.png');
			try {
				// Inputs explicitos (ffmpeg glob no funciona en Windows).
				const args = ['-y'];
				for (const img of imgs) args.push('-i', img);
				const grid = Math.ceil(Math.sqrt(imgs.length));
				const inputs = imgs.map((_, i) => `[${i}:v]`).join('');
				const layout = [];
				for (let r = 0; r < grid; r++) {
					for (let c = 0; c < grid; c++) {
						if (r * grid + c >= imgs.length) break;
						layout.push(`${c === 0 ? 0 : 'w0*' + c}_${r === 0 ? 0 : 'h0*' + r}`);
					}
				}
				args.push('-filter_complex', `${inputs}xstack=inputs=${imgs.length}:layout=${layout.join('|')}[v]`, '-map', '[v]', '-frames:v', '1', montagePath);
				execFileSync('ffmpeg', args, { stdio: 'ignore' });
				const text = await chat(model, [{ role: 'user', content: `${basePrompt}\nLa imagen es una hoja de contacto con ${imgs.length} frames en rejilla ${grid}x${grid} (orden de lectura: filas de arriba a abajo, izquierda a derecha). Describe que se ve en cada frame y que cambia entre ellos.`, images: [fs.readFileSync(montagePath).toString('base64')] }]);
				report.push(`![hoja de contacto](${path.basename(montagePath)})`, text, '');
				process.stdout.write(`[ollama] secuencia (montage) analizada\n`);
			} catch (e) {
				report.push(`(no se pudo crear/analizar el montaje: ${e.message})`, '');
			}
		}
	}

	fs.writeFileSync(outFile, report.join('\n'));
	console.log(`[ollama] informe: ${outFile}`);
}

main().catch((e) => { console.error(e); process.exit(1); });
