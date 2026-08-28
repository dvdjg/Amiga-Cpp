#!/usr/bin/env node
/**
 * ai-analyze.mjs — Orquestador para la IA (y para uso manual).
 *
 * Encadena la captura de perfil por el canal lateral (2346), la extraccion de
 * frames y el analisis con Ollama LOCAL (sin gastar tokens de la nube), y al
 * terminar imprime el informe final por stdout para que la IA pueda leerlo
 * directamente. Deja evidencia en disco:
 *
 *   out/profile/<outName>/{<outName>.profile.bin, frame_*.jpg/png,
 *                         profile-summary.json, ollama-report.md}
 *
 * Reutiliza los scripts probados:
 *   - tools/profile/capture-profile.mjs   (canal lateral 2346)
 *   - tools/profile/profile-extract.mjs   (parser del .bin)
 *   - tools/profile/ollama-analyze.mjs    (Ollama local: meta + vision)
 *
 * Uso:
 *   node tools/profile/ai-analyze.mjs <outName> [frames] \
 *       [--prompt "lo que se espera ver" | --prompt-file FILE] \
 *       [--model MODEL] [--text-model MODEL] [--base URL] [--mode meta|frames|montage|all] \
 *       [--wait-cmd 'runstatus 0x<addr>' --contains READY] [--wait-ms MS] \
 *       [--lock-owner NOMBRE] [--port 2346] [--demo <demo>]
 *
 * Requisitos:
 *   - WinUAE-DBG corriendo con la demo cargada y WINUAE_GDB_PERSIST_LISTENER=1.
 *   - Ollama local activo (por defecto http://127.0.0.1:11434).
 *
 * Con --demo <demo> el propio script lanza WinUAE como hijo (asi el emulador y
 * el canal lateral sobreviven a la captura), espera READY por runstatus y
 * apaga WinUAE al terminar. NOTA: el analisis completo (meta + vision) puede
 * tardar 2-4 minutos con modelos locales; usa --mode meta para un avance rapido.
 */
import { spawnSync } from 'child_process';
import * as fs from 'fs';
import * as path from 'path';
import { fileURLToPath } from 'url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(__dirname, '../..');
const NODE = process.execPath;
function argValue(name, fallback) {
	const i = process.argv.indexOf(name);
	return i >= 0 && i + 1 < process.argv.length ? process.argv[i + 1] : fallback;
}

function usage() {
	console.error(`Uso: ai-analyze.mjs <outName> [frames] [opciones]
  --prompt TEXTO | --prompt-file FILE   Que se espera ver (intencion del programador).
  --model M   Modelo de vision (Ollama)  (def: qwen3-vl:8b-instruct-q8_0)
  --text-model M  Modelo de texto (pre-analisis meta) (def: qwen3:8b)
  --base URL      URL base de Ollama     (def: http://127.0.0.1:11434)
  --mode MODO     meta|frames|montage|all  (def: all)
  --wait-cmd 'CMD' --contains TXT   Espera a que el canal devuelva TXT antes de capturar.
  --wait-ms MS    Espera fija en ms antes de capturar.
  --lock-owner N  Owner del debug lock  (def: ai-analyze)
  --port N        Puerto del canal lateral (def: 2346)
  --demo DEMO     Lanza la demo via runner y espera READY antes de capturar.
  --demo-timeout-ms MS   Timeout esperando READY con --demo (def: 90000)
Ejemplos:
  node tools/profile/ai-analyze.mjs demo101 6 --prompt "scroll horizontal fino; verificar que los tiles se ensamblan sin salto de 16px"
  node tools/profile/ai-analyze.mjs demo101 6 --demo demos/101_ehb_tile_scroll_driver --prompt "scroll horizontal fino; verificar que los tiles se ensamblan sin salto de 16px"
  node tools/profile/ai-analyze.mjs demo050 4 --prompt-file tools/profile/prompts/050-blitter-bobs.md`);
	process.exit(2);
}

function run(label, script, args) {
	const res = spawnSync(NODE, [script, ...args], { cwd: ROOT, stdio: 'inherit', encoding: 'utf8' });
	if (res.error) {
		throw new Error(`error ejecutando ${script}: ${res.error.message}`);
	}
	if (res.status !== 0) {
		throw new Error(`${label} fallo (exit ${res.status})`);
	}
}

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

async function main() {
	const outName = process.argv[2];
	if (!outName) usage();
	const frames = process.argv[3] ? process.argv[3] : '4';

	const prompt = argValue('--prompt', null);
	const promptFile = argValue('--prompt-file', null);
	const model = argValue('--model', null);
	const textModel = argValue('--text-model', null);
	const base = argValue('--base', null);
	const mode = argValue('--mode', null);
	const waitCmd = argValue('--wait-cmd', null);
	const contains = argValue('--contains', null);
	const waitMs = argValue('--wait-ms', null);
	const lockOwner = argValue('--lock-owner', 'ai-analyze');
	const port = argValue('--port', null);
	const demo = argValue('--demo', null);
	const demoTimeoutMs = parseInt(argValue('--demo-timeout-ms', '60000'), 10);

	const outDir = path.join(ROOT, 'out', 'profile', outName);
	const bin = path.join(outDir, `${outName}.profile.bin`);

	// 0) Opcional: lanza la demo directamente (ai-analyze queda como padre de
	//    WinUAE, asi el emulador y el canal lateral sobreviven a la captura).
	let conn = null;
	try {
		// 0) Opcional: lanza la demo directamente (ai-analyze queda como padre de
		//    WinUAE, asi el emulador y el canal lateral sobreviven a la captura).
		if (demo) {
			console.log(`[ai-analyze] lanzando ${demo} (WinUAE directo) y esperando READY...`);
			const { launchDemoAndWaitReady } = await import('./launch-winuae.mjs');
			const launched = await launchDemoAndWaitReady({
				demo,
				port: port ? parseInt(String(port), 10) : 2346,
				readyTimeoutMs: demoTimeoutMs,
			});
			conn = launched.conn;
			console.log('[ai-analyze] demo READY');
			// Asentamiento corto si no hay espera explicita.
			if (!waitCmd && !waitMs) await sleep(1000);
		}

		// 1) Captura por canal lateral.
		const captureArgs = [path.join('tools', 'profile', 'capture-profile.mjs'), bin, frames, '--lock-owner', lockOwner];
		for (const [flag, val] of [
			['--wait-cmd', waitCmd], ['--contains', contains], ['--wait-ms', waitMs], ['--port', port],
		]) {
			if (val) captureArgs.push(flag, String(val));
		}
		run('captura', captureArgs[0], captureArgs.slice(1));

		// 2) Extraccion de frames.
		run('extraccion', path.join('tools', 'profile', 'profile-extract.mjs'), [bin, outDir]);

		// 3) Analisis con Ollama local.
		const analyzeArgs = [path.join('tools', 'profile', 'ollama-analyze.mjs'), outDir];
		for (const [flag, val] of [
			['--prompt', prompt], ['--prompt-file', promptFile], ['--model', model],
			['--text-model', textModel], ['--base', base], ['--mode', mode],
		]) {
			if (val) analyzeArgs.push(flag, String(val));
		}
		run('analisis', analyzeArgs[0], analyzeArgs.slice(1));

		// 4) Vuelca el informe final por stdout.
		const report = path.join(outDir, 'ollama-report.md');
		if (fs.existsSync(report)) {
			console.log(`\n===== INFORME (${report}) =====\n`);
			console.log(fs.readFileSync(report, 'utf8'));
		} else {
			console.error(`[ai-analyze] no se encontro informe en ${report}`);
		}
	} finally {
		// 5) Apaga el WinUAE lanzado por --demo (aunque algo falle).
		if (conn) {
			try { await conn.disconnect(true); } catch { /* noop */ }
		}
	}
}

await main();
