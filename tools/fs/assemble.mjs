// Ensambla un fragmento 68k a bytes crudos con **vasm** (-Fbin), en vez de escribir las
// instrucciones a mano (un `moveq` mal codificado dio `answer=106` en vez de 42).
//
// vasm lo aporta el toolchain (extension Bartman): se busca por `AMIGA_BIN_PATH` y por las
// rutas habituales de la extension. Ver tools/fs/README.md.
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { spawnSync } from 'node:child_process';

const VASM_NAME = process.platform === 'win32' ? 'vasmm68k_mot.exe' : 'vasmm68k_mot';

function vasmCandidates() {
	const out = [];
	if (process.env.AMIGA_BIN_PATH) out.push(path.join(process.env.AMIGA_BIN_PATH, VASM_NAME));
	const home = os.homedir();
	for (const v of ['1.8.1', '1.7.9']) {
		for (const root of ['.vscode', '.cursor']) {
			out.push(path.join(home, `${root}/extensions/bartmanabyss.amiga-debug-${v}/bin/win32/${VASM_NAME}`));
		}
	}
	return out;
}

/// Ruta del vasm disponible (o `null`).
export function findVasm() {
	return vasmCandidates().find((p) => fs.existsSync(p)) || null;
}

/// Ensambla `source` (sintaxis motorola) y devuelve los bytes crudos del codigo.
export function assemble68k(source) {
	const vasm = findVasm();
	if (!vasm) {
		throw new Error('vasm no encontrado (define AMIGA_BIN_PATH o instala la extension Bartman)');
	}
	const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'eng-asm-'));
	const src = path.join(dir, 'stub.s');
	const out = path.join(dir, 'stub.bin');
	try {
		fs.writeFileSync(src, source, 'ascii');
		const r = spawnSync(vasm, ['-Fbin', '-o', out, src], { encoding: 'utf8' });
		if (r.status !== 0 || !fs.existsSync(out)) {
			throw new Error('vasm fallo: ' + (r.stdout || '') + (r.stderr || ''));
		}
		return fs.readFileSync(out);
	} finally {
		fs.rmSync(dir, { recursive: true, force: true });
	}
}
