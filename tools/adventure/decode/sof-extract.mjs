#!/usr/bin/env node
// Extrae el audio digital de un contenedor `.sof` (ediciones Talkie, FLAC embebido) a ficheros
// estándar reproducibles (`*.flac`) + manifiesto JSON. Formato y citas: ver `sof.mjs`.
//
// Uso:
//   node tools/adventure/decode/sof-extract.mjs <juego|fichero.sof> [--out <dir>] [--limit N]
//
// Con `<juego>` resuelve `out/adventure/<juego>/` y busca el `.sof|.sou|.sog|.so3` del juego
// (o lo toma de `game.json`/inbox). Salida por defecto: `<ws>/decoded/audio/` con
// `manifest.json` (adv-sof/1).

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { extractSof, readSofIndex } from './sof.mjs';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const REPO = path.resolve(HERE, '../../..');

function parseArgs(argv) {
	const out = { _: [] };
	for (let i = 0; i < argv.length; i++) {
		const a = argv[i];
		if (a.startsWith('--')) out[a.slice(2)] = argv[++i];
		else out._.push(a);
	}
	return out;
}

const args = parseArgs(process.argv.slice(2));
const target = args._[0];
if (!target) {
	console.error('Uso: node tools/adventure/decode/sof-extract.mjs <juego|fichero.sof> [--out <dir>] [--limit N]');
	process.exit(2);
}

const ws = path.join(REPO, 'out', 'adventure', target);
let sofFile;
let outDir;
if (fs.existsSync(ws) && fs.statSync(ws).isDirectory()) {
	const candidates = [];
	const scan = (dir) => {
		for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
			const p = path.join(dir, e.name);
			if (e.isDirectory() && e.name !== 'raw' && e.name !== 'cook' && e.name !== 'decoded') scan(p);
			else if (e.isFile() && /\.(sof|sou|sog|so3)$/i.test(e.name)) candidates.push(p);
		}
	};
	scan(ws);
	// Fallback: junto a la fuente del game.json (los .sof viven con los datos del juego).
	if (candidates.length === 0 && fs.existsSync(path.join(ws, 'game.json'))) {
		const src = JSON.parse(fs.readFileSync(path.join(ws, 'game.json'), 'utf8')).source ?? '';
		const dir = path.dirname(src);
		if (dir && fs.existsSync(dir)) {
			for (const e of fs.readdirSync(dir)) if (/\.(sof|sou|sog|so3)$/i.test(e)) candidates.push(path.join(dir, e));
		}
	}
	if (candidates.length === 0) {
		console.error(`[sof] no se encontró contenedor de audio para ${target} (pasa el fichero .sof)`);
		process.exit(1);
	}
	sofFile = candidates[0];
	outDir = args.out ? String(args.out) : path.join(ws, 'decoded', 'audio');
} else {
	sofFile = path.resolve(target);
	if (!fs.existsSync(sofFile)) {
		console.error(`[sof] no existe ${sofFile}`);
		process.exit(1);
	}
	outDir = args.out ? String(args.out) : path.join(path.dirname(sofFile), 'decoded_audio');
}

const limit = args.limit ? Number(args.limit) : 0;
if (limit > 0) {
	// Extracción parcial (pruebas): recorta el manifiesto tras validar el índice.
	const { entries, dataStart } = readSofIndex(sofFile);
	fs.mkdirSync(outDir, { recursive: true });
	const fd = fs.openSync(sofFile, 'r');
	const manifest = { format: 'adv-sof/1', source: path.basename(sofFile), entries: [] };
	try {
		for (const e of entries.slice(0, limit)) {
			const buf = Buffer.alloc(e.size);
			fs.readSync(fd, buf, 0, e.size, e.abs);
			const ext = buf.subarray(0, 4).toString('latin1') === 'fLaC' ? 'flac' : 'bin';
			const name = `${String(e.i).padStart(5, '0')}.${ext}`;
			fs.writeFileSync(path.join(outDir, name), buf);
			manifest.entries.push({ ...e, ext, file: name });
		}
	} finally {
		fs.closeSync(fd);
	}
	fs.writeFileSync(path.join(outDir, 'manifest.json'), JSON.stringify(manifest, null, 2) + '\n');
	console.log(`[sof] ${manifest.entries.length}/${entries.length} pistas -> ${outDir}`);
} else {
	const manifest = extractSof(sofFile, outDir);
	fs.writeFileSync(path.join(outDir, 'manifest.json'), JSON.stringify(manifest, null, 2) + '\n');
	const flac = manifest.entries.filter((e) => e.ext === 'flac').length;
	console.log(`[sof] ${manifest.entries.length} pistas (${flac} flac) -> ${outDir}/manifest.json`);
}
