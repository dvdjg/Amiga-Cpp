#!/usr/bin/env node
/**
 * read-debugperiph.mjs — Lanza una demo y lee el periférico de depuración
 * (0xB70000) con el comando `debugperiph <sub>` del monitor de WinUAE-DBG.
 *
 * Permite leer, sin depender de la tool MCP `winuae_debugperiph` (que requiere
 * una sesión MCP), la telemetría que una demo instrumentada publica:
 *   - `counters`: valores de contador (p. ej. los ciclos del benchmark de la
 *     demo 063, slots 0/1/2 = fire_cpp / fire_asm / t0_raw).
 *   - `checkpoints`: checkpoints registrados (p. ej. la demo 101).
 *   - `console`: texto de consola acumulado.
 *
 * Lanza WinUAE igual que el runner (escribe la startup-sequence, arranca
 * parado y hace `continue`), deja que la demo ejecute su primer frame y lee el
 * periférico por el comando de monitor. No depende de la resolución de
 * `g_eng_run_status` por el canal lateral.
 *
 * Uso:
 *   node tools/debug/read-debugperiph.mjs <demo> [--sub <sub>] [--wait-ms N]
 *
 * Ejemplo (benchmark 063):
 *   node tools/debug/read-debugperiph.mjs demos/amiga/063_fire_cpp_vs_asm --sub counters --wait-ms 12000
 */
import * as path from 'path';
import fs from 'fs';
import { fileURLToPath, pathToFileURL } from 'url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(__dirname, '..', '..');
const MCP = path.join(path.dirname(ROOT), 'mcp-winuae-emu');
const { prepareDemo, detectWinUAE } = await import(
	pathToFileURL(path.join(ROOT, 'tools', 'profile', 'launch-winuae.mjs')).href
);
const SLEEP = (ms) => new Promise((r) => setTimeout(r, ms));

function argValue(name, fallback) {
	const i = process.argv.indexOf(name);
	return i >= 0 && i + 1 < process.argv.length ? process.argv[i + 1] : fallback;
}

const demo = process.argv[2];
if (!demo) {
	console.error('Uso: read-debugperiph.mjs <demo> [--sub <sub>] [--wait-ms N]');
	process.exit(2);
}
const sub = String(argValue('--sub', '')).trim();
const waitMs = parseInt(argValue('--wait-ms', '12000'), 10);

process.env.WINUAE_GDB_PERSIST_LISTENER = '1';
process.env.WINUAE_USE_LEGACY_LAUNCH = '1';
const { WinUAEConnection } = await import(pathToFileURL(path.join(MCP, 'dist', 'winuae-connection.js')).href);

// Igual que run-demo.ts: prepara la startup-sequence para lanzar a.exe desde dh1
// con un stack generoso, para que la demo arranque de forma fiable.
const { winuaePath } = detectWinUAE();
const extRoot = path.dirname(path.dirname(winuaePath)); // <ext>/bin/win32 -> <ext>
const startupPath = path.join(extRoot, 'bin', 'dh0', 's', 'startup-sequence');
fs.mkdirSync(path.dirname(startupPath), { recursive: true });
fs.writeFileSync(startupPath, 'stack 131072\ncd dh1:\n:a.exe\n', 'utf8');

const { configPath } = prepareDemo(demo);
const conn = new WinUAEConnection({ winuaePath, configFile: configPath, gdbPort: 2345 });
await conn.connect({ forceBreak: false, initializeStopped: true });
await conn.getProtocol().continue();
console.log(`[read-debugperiph] esperando ${waitMs} ms para que la demo arranque y corra su benchmark/frame...`);
await SLEEP(waitMs);

const cmd = sub ? `debugperiph ${sub}` : 'debugperiph';
const reply = await conn.getProtocol().sendMonitorCommand(cmd, 10000);
const text = Buffer.from(reply, 'hex').toString('utf8').trim();
console.log(`[read-debugperiph] ${cmd}:\n${text}`);

await conn.disconnect(true);
console.log('[read-debugperiph] ok');
process.exit(0);
