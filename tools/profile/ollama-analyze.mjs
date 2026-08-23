#!/usr/bin/env node
/**
 * ollama-analyze.mjs — Analiza los frames extraidos de un perfil con un modelo
 * de vision local (Ollama) para describir con precision que hay en pantalla.
 *
 * Uso:
 *   node tools/profile/ollama-analyze.mjs <outDir> [--model llava:13b]
 *       [--base http://127.0.0.1:11434] [--frames 0,1,2] [--contact-sheet]
 *       [--prompt "texto extra"] [--out reporte.md]
 *
 * outDir es el directorio producido por profile-extract.mjs (frame_0000.jpg +
 * profile-summary.json).
 */
import * as fs from 'fs';
import * as path from 'path';

function argValue(name, fallback) {
	const i = process.argv.indexOf(name);
	return i >= 0 && i + 1 < process.argv.length ? process.argv[i + 1] : fallback;
}

function usage() {
	console.error('Uso: ollama-analyze.mjs <outDir> [--model MODEL] [--base URL] [--frames a,b,c] [--prompt TEXTO] [--out FILE]');
	process.exit(2);
}

const BASE_PROMPT = `Eres un analizador de capturas de pantalla de una demo Amiga 500 (OCS/ECS, modo EHB). Describe con PRECISION lo que se ve en la imagen: colores de fondo, formas/bandas/blobs visibles, posiciones aproximadas (izquierda/centro/derecha, arriba/medio/abajo), si hay artefactos visuales (negro interno, tearing, bandas incorrectas, parpadeo) y cualquier anomalia. Si es parte de una secuencia, indica que ha cambiado respecto al frame anterior. Responde de forma concreta y breve (max 120 palabras).`;

async function main() {
	const outDir = process.argv[2];
	if (!outDir) usage();
	const model = argValue('--model', process.env.OLLAMA_MODEL || 'llava:13b');
	const base = argValue('--base', process.env.OLLAMA_BASE || 'http://127.0.0.1:11434');
	const framesArg = argValue('--frames', null);
	const promptExtra = argValue('--prompt', '');
	const outFile = argValue('--out', path.join(outDir, 'ollama-report.md'));
	const useContactSheet = process.argv.includes('--contact-sheet');

	const summaryPath = path.join(outDir, 'profile-summary.json');
	if (!fs.existsSync(summaryPath)) {
		console.error('No existe ' + summaryPath + '. Ejecuta primero profile-extract.mjs');
		process.exit(1);
	}
	const summary = JSON.parse(fs.readFileSync(summaryPath, 'utf8'));

	let frameIndices = summary.frames.map((f) => f.frame);
	if (framesArg) frameIndices = framesArg.split(',').map((s) => parseInt(s.trim(), 10));

	async function chat(messages) {
		const res = await fetch(`${base}/api/chat`, {
			method: 'POST',
			headers: { 'content-type': 'application/json' },
			body: JSON.stringify({ model, messages, stream: false })
		});
		if (!res.ok) throw new Error(`Ollama HTTP ${res.status}: ${await res.text()}`);
		const data = await res.json();
		return data.message?.content ?? '';
	}

	const report = [`# Análisis de pantalla (modelo ${model})`, '', `Fuente: ${summary.file} · ${summary.frames.length} frame(s)`, ''];

	let previousText = '';
	for (const idx of frameIndices) {
		const meta = summary.frames.find((f) => f.frame === idx);
		if (!meta) continue;
		const imgPath = path.join(outDir, meta.screenshot);
		if (!fs.existsSync(imgPath)) continue;

		const b64 = fs.readFileSync(imgPath).toString('base64');
		const userMsg = [
			`Frame ${idx} de ${summary.frames.length}.`,
			meta.registers && meta.registers.BPLCON0 ? `BPLCON0=${meta.registers.BPLCON0} DDFSTRT=${meta.registers.DDFSTRT} DIW=${meta.registers.DIWSTRT}-${meta.registers.DIWSTOP}` : '',
			meta.bitplanes && meta.bitplanes.length ? `Bitplanes: ${meta.bitplanes.map((b) => '0x' + b.address.toString(16) + ' ' + b.width + 'x' + b.height + 'x' + b.numPlanes).join(', ')}` : '',
			meta.palette && meta.palette.length ? `Paleta: ${meta.palette.map((p) => p.numEntries).join(',')} entradas` : '',
			previousText ? `Contexto frame anterior: ${previousText}` : '',
			BASE_PROMPT,
			promptExtra ? 'Requisito extra: ' + promptExtra : ''
		].filter(Boolean).join('\n');

		let text;
		if (useContactSheet) {
			// Envia una hoja de contacto con todos los frames de golpe (ahorra llamadas).
			const allB64 = summary.frames.map((f) => fs.existsSync(path.join(outDir, f.screenshot)) ? fs.readFileSync(path.join(outDir, f.screenshot)).toString('base64') : null).filter(Boolean);
			text = await chat([{ role: 'user', content: `${BASE_PROMPT}\nAqui van ${allB64.length} frames en orden. Describe cada uno brevemente y que cambia entre ellos.\n${promptExtra}`, images: allB64 }]);
			report.push(`## Frames ${frameIndices.join(', ')} (hoja de contacto)`, '', text, '');
			break;
		} else {
			text = await chat([{ role: 'user', content: userMsg, images: [b64] }]);
			previousText = text;
			report.push(`## Frame ${idx}`, '', `![frame ${idx}](${meta.screenshot})`, '', text, '');
			process.stdout.write(`[ollama] frame ${idx} analizado\n`);
		}
	}

	fs.writeFileSync(outFile, report.join('\n'));
	console.log(`[ollama] informe: ${outFile}`);
}

main().catch((e) => { console.error(e); process.exit(1); });
