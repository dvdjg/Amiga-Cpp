#!/usr/bin/env node
/**
 * step-memory.mjs — Avanza por breakpoints sobre un WinUAE-DBG vivo y analiza
 * memoria (en especial el frame buffer) en cada parada.
 *
 * Conecta al socket GDB (2345) con el cliente RSP de mcp-winuae-emu. Pone un
 * breakpoint, continua, y en cada parada lee/rendera una region de memoria.
 *
 * Uso:
 *   node tools/debug/step-memory.mjs [--port 2345] [--elf <elf>]
 *       --bp <simbolo|0xaddr> \
 *       [--render <addr|simbolo> <width> <height> <planes> [planar|interleaved] [--palette 'rrggbb,...']]
 *       [--read <addr|simbolo> <len>]
 *       [--steps N] [--out outDir]
 *
 * Ejemplo:
 *   node tools/debug/step-memory.mjs --elf out/debug-current/current.elf \
 *       --bp 0xc0f986 \
 *       --render 0x15058 320 256 6 planar --palette '000,024,048,...' \
 *       --steps 5
 */
import { createRequire } from 'module';
import * as path from 'path';
import * as fs from 'fs';
import { pathToFileURL } from 'url';

const require = createRequire(import.meta.url);
const MCP = path.join(path.dirname(new URL(import.meta.url).pathname), '..', '..', '..', 'mcp-winuae-emu', 'dist');

const { GdbProtocol } = await import(pathToFileURL(path.join(MCP, 'gdb-protocol.js')).href);
const { decodePlanarBitmap, encodePngRgba } = await import(pathToFileURL(path.join(MCP, 'bitmap-decode.js')).href);

function argValue(name, fallback) {
	const i = process.argv.indexOf(name);
	return i >= 0 && i + 1 < process.argv.length ? process.argv[i + 1] : fallback;
}
function usage() {
	console.error('Uso: step-memory.mjs --bp <simbolo|0xaddr> [--elf elf] [--render addr w h planes [layout]] [--read addr len] [--steps N] [--port 2345]');
	process.exit(2);
}

function resolveAddr(spec, elfPath, loadOffset) {
	if (/^0x[0-9a-f]+$/i.test(spec)) return parseInt(spec, 16);
	if (!elfPath) throw new Error('No se puede resolver simbolo sin --elf');
	// nm del toolchain de la extension
	const bin = path.join(process.env.USERPROFILE, '.vscode', 'extensions', 'bartmanabyss.amiga-debug-1.8.1', 'bin', 'win32', 'opt', 'bin');
	const nm = path.join(bin, process.platform === 'win32' ? 'm68k-amiga-elf-nm.exe' : 'm68k-amiga-elf-nm');
	const out = require('child_process').execSync(`"${nm}" -n "${elfPath}" 2>/dev/null`, { encoding: 'utf8' });
	for (const line of out.split('\n')) {
		const m = /^([0-9a-f]+)\s+[Tt]\s+(\S+)$/.exec(line.trim());
		if (m && (m[2] === spec || m[2].endsWith('::' + spec))) {
			return parseInt(m[1], 16) + loadOffset;
		}
	}
	throw new Error('Simbolo no encontrado: ' + spec);
}

async function main() {
	const port = parseInt(argValue('--port', '2345'), 10);
	const elf = argValue('--elf', null);
	const bp = argValue('--bp', null);
	const renderSpec = argValue('--render', null);
	const readSpec = argValue('--read', null);
	const steps = parseInt(argValue('--steps', '0'), 10);
	const outDir = argValue('--out', path.join(process.cwd(), 'out', 'step-memory'));
	if (!bp) usage();

	const proto = new GdbProtocol();
	await proto.connect('127.0.0.1', port, {});
	console.log('[step-memory] conectado a GDB');

	let loadOffset = 0;
	try {
		const off = await proto.queryOffsets();
		const m = /"([0-9a-fA-F]+)/.exec(off);
		if (m) loadOffset = parseInt(m[1], 16) - 0x400;
	} catch (e) { console.warn('[step-memory] queryOffsets fallo, loadOffset=0'); }
	console.log(`[step-memory] loadOffset=0x${loadOffset.toString(16)}`);

	const bpAddr = resolveAddr(bp, elf, loadOffset);
	console.log(`[step-memory] breakpoint en 0x${bpAddr.toString(16)}`);
	await proto.setBreakpoint(bpAddr);
	await proto.continue();
	const stop1 = await proto.waitForStop(60000);
	console.log(`[step-memory] parada: ${stop1}`);

	fs.mkdirSync(outDir, { recursive: true });

	// Render/read de la region en cada parada.
	const iterations = steps + 1;
	for (let it = 0; it < iterations; it++) {
		if (it > 0) {
			await proto.step();
			await proto.waitForStop(10000);
			console.log(`[step-memory] step ${it}`);
		}

		if (renderSpec) {
			const parts = renderSpec.split(' ');
			const [addrSpec, w, h, planes, layoutRaw] = parts;
			const width = parseInt(w, 10), height = parseInt(h, 10), planesN = parseInt(planes, 10);
			const layout = layoutRaw === 'interleaved' ? 'interleaved' : 'planar';
			const paletteArg = argValue('--palette', null);
			const addr = resolveAddr(addrSpec, elf, loadOffset);
			const rowBytes = Math.ceil(width / 8);
			const total = layout === 'interleaved' ? height * rowBytes * planesN : planesN * rowBytes * height;
			const bitmap = await proto.readMemory(addr, total);
			const decoded = decodePlanarBitmap(bitmap, {
				width, height, depth: planesN, rowBytes,
				layout, colorMode: paletteArg ? 'plain' : 'ehb'
			}, undefined, paletteArg ? paletteArg.split(',') : undefined);
			const png = encodePngRgba(decoded.width, decoded.height, decoded.rgba);
			const p = path.join(outDir, `step_${it}.png`);
			fs.writeFileSync(p, png);
			console.log(`[step-memory] frame buffer 0x${addr.toString(16)} -> ${p}`);
		}

		if (readSpec) {
			const [addrSpec, len] = readSpec.split(' ');
			const addr = resolveAddr(addrSpec, elf, loadOffset);
			const data = await proto.readMemory(addr, parseInt(len, 10));
			const p = path.join(outDir, `mem_${it}.bin`);
			fs.writeFileSync(p, data);
			console.log(`[step-memory] memoria 0x${addr.toString(16)} (${data.length}) -> ${p}`);
		}
	}

	await proto.clearBreakpoint(bpAddr);
	await proto.disconnect();
	console.log(`[step-memory] OK, salida en ${outDir}`);
}

main().catch((e) => { console.error(e); process.exit(1); });
