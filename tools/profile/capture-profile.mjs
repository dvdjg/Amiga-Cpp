#!/usr/bin/env node
/**
 * capture-profile.mjs — Captura un perfil de WinUAE-DBG via canal lateral.
 *
 * El canal lateral (127.0.0.1:2346) permite pedir profiling sin competir por el
 * socket GDB. Este script encola la orden `profile`, espera a que termine y
 * deja el binario en outFile. Opcionalmente espera a que se cumpla una condicion
 * antes de iniciar la captura (p. ej. que la demo llegue a READY o a un frame).
 *
 * Uso:
 *   node tools/profile/capture-profile.mjs <outFile> [frames] [--port 2346]
 *       [--wait-cmd '<comando canal>' --contains '<texto>'] [--wait-ms <ms>]
 *
 * Ejemplos:
 *   # Espera a que g_eng_run_status diga READY (direccion dada) y captura 12 frames
 *   node tools/profile/capture-profile.mjs out/p.bin 12 \
 *       --wait-cmd 'runstatus 0x<addr>' --contains 'READY'
 *   # Simplemente espera 3s y captura 1 frame
 *   node tools/profile/capture-profile.mjs out/p.bin 1 --wait-ms 3000
 */
import * as net from 'net';
import * as path from 'path';

function argValue(name, fallback) {
	const i = process.argv.indexOf(name);
	return i >= 0 && i + 1 < process.argv.length ? process.argv[i + 1] : fallback;
}

function hasFlag(name) {
	return process.argv.includes(name);
}

function usage() {
	console.error('Uso: capture-profile.mjs <outFile> [frames] [--port 2346] [--wait-cmd <cmd> --contains <txt>] [--wait-ms <ms>]');
	process.exit(2);
}

/** Envia una orden al canal lateral y devuelve la primera linea JSON. */
function command(port, cmd, timeoutMs = 10000) {
	return new Promise((resolve, reject) => {
		const sock = net.createConnection({ host: '127.0.0.1', port });
		sock.setEncoding('utf8');
		let pending = '';
		let greeting = false;
		const timer = setTimeout(() => { sock.destroy(); reject(new Error('timeout en canal lateral: ' + cmd)); }, timeoutMs);
		sock.on('data', (chunk) => {
			pending += chunk;
			for (;;) {
				const eol = pending.indexOf('\n');
				if (eol < 0) return;
				const line = pending.slice(0, eol).trim();
				pending = pending.slice(eol + 1);
				if (!greeting) { greeting = true; sock.write(cmd + '\n'); continue; }
				clearTimeout(timer);
				sock.end();
				resolve(line);
				return;
			}
		});
		sock.on('error', (e) => { clearTimeout(timer); reject(e); });
	});
}

async function main() {
	const outFile = process.argv[2];
	if (!outFile) usage();
	const frames = parseInt(process.argv[3] || '1', 10);
	const port = parseInt(argValue('--port', process.env.WINUAE_SIDE_CHANNEL_PORT || '2346'), 10);
	const waitCmd = argValue('--wait-cmd', null);
	const contains = argValue('--contains', null);
	const waitMs = parseInt(argValue('--wait-ms', '0'), 10);

	const fs = await import('fs');
	fs.mkdirSync(path.dirname(path.resolve(outFile)), { recursive: true });

	// Condicion: espera un comando que contenga un texto (ej. runstatus READY).
	if (waitCmd && contains) {
		const deadline = Date.now() + (parseInt(argValue('--wait-timeout', '20000'), 10));
		for (;;) {
			let out = '';
			try { out = await command(port, waitCmd, 3000); } catch (e) { /* reintenta */ }
			if (out.includes(contains)) { console.log(`[capture-profile] condicion cumplida: ${waitCmd} -> ${out}`); break; }
			if (Date.now() > deadline) { console.error('[capture-profile] timeout esperando condicion'); process.exit(1); }
			await new Promise((r) => setTimeout(r, 200));
		}
	} else if (waitMs > 0) {
		console.log(`[capture-profile] esperando ${waitMs} ms`);
		await new Promise((r) => setTimeout(r, waitMs));
	}

	// Encola el profile. `profile` requiere lock asistente.
	const lockOwner = argValue('--lock-owner', 'profile-capture');
	const lockReq = await command(port, `lock acquire ${lockOwner} assist`, 5000);
	console.log(`[capture-profile] lock: ${lockReq}`);
	if (!lockReq.includes('"ok":true') && !lockReq.includes('"acquired"') && !lockReq.includes('already')) {
		console.error('[capture-profile] no se pudo adquirir lock assist');
		process.exit(1);
	}
	const queued = await command(port, `profile ${frames} "${outFile}"`, 10000);
	const queuedObj = JSON.parse(queued);
	console.log(`[capture-profile] encolado: ${queued}`);
	if (!queuedObj.ok) { console.error('[capture-profile] fallo al encolar'); process.exit(1); }
	const id = queuedObj.id;

	// Espera a que termine la accion.
	for (let i = 0; i < 120; i++) {
		const st = await command(port, `action status ${id}`, 10000);
		if (st.includes('"ok":true') || st.includes('"status":"done"') || st.includes('"result"')) {
			console.log(`[capture-profile] action: ${st}`);
			break;
		}
		await new Promise((r) => setTimeout(r, 250));
	}
	// Confirma idle (profile-status -> done).
	for (let i = 0; i < 120; i++) {
		const st = await command(port, 'profile-status', 10000);
		if (st.includes('"done"')) { console.log(`[capture-profile] profile-status: ${st}`); break; }
		await new Promise((r) => setTimeout(r, 250));
	}

	// Libera el lock asistent.
	try { await command(port, `lock release ${lockOwner}`, 3000); } catch (e) { /* noop */ }
	if (!fs.existsSync(outFile)) {
		console.error('[capture-profile] no se genero ' + outFile);
		process.exit(1);
	}
	console.log(`[capture-profile] OK ${fs.statSync(outFile).size} bytes -> ${outFile}`);}

main().catch((e) => { console.error(e); process.exit(1); });
