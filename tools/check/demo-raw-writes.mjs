// ---------------------------------------------------------------------------
// Gate `demo-raw-writes`: inventario de escrituras directas por `Block::view.data()`
// (o `view.data()`) en demos/juegos. La reserva tipada ya acota el buffer, pero una
// escritura cruda puede salirse de su rango lógico; el objetivo es (1) no añadir
// escapes nuevos y (2) ir sustituyéndolos por primitivas del engine cuando existan.
//
//   tools/check/demo-raw-frontier.txt : fronteras declaradas (path # razón). Una demo
//       técnica con escritura metal-a-metal justificada vive aquí; el gate la exime.
//   tools/check/demo-raw-baseline.txt  : deuda histórica (path por línea). Solo puede
//       encoger: al migrar una demo a primitivas del engine se borra su línea.
//
// Uso: node tools/check/demo-raw-writes.mjs [--list|--update-baseline]
// ---------------------------------------------------------------------------
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const FRONTIER_PATH = path.join(ROOT, 'tools/check/demo-raw-frontier.txt');
const BASELINE_PATH = path.join(ROOT, 'tools/check/demo-raw-baseline.txt');
const SCAN_DIRS = ['demos', 'games'];

function listSources(dir) {
	const out = [];
	const walk = (d) => {
		let entries;
		try {
			entries = fs.readdirSync(d, { withFileTypes: true });
		} catch {
			return;
		}
		for (const e of entries) {
			const p = path.join(d, e.name);
			if (e.isDirectory()) {
				if (e.name === 'out' || e.name === 'vision' || e.name === 'data') continue;
				walk(p);
			} else if (/\.(cpp|hpp|h)$/.test(e.name)) {
				out.push(p);
			}
		}
	};
	walk(dir);
	return out;
}

/// Línea que ESCRIBE a través de una vista de bloque (`view.data()`). Se excluyen las
/// lecturas puras (comparaciones, `constants`, copias desde la vista).
function write_lines(text) {
	const out = [];
	const lines = text.split(/\r?\n/);
	for (let i = 0; i < lines.length; ++i) {
		const line = lines[i];
		if (!line.includes('view.data()')) continue;
		if (line.trimStart().startsWith('//')) continue;
		// Una dirección `reinterpret_cast<const ...*>` es lectura/atributo, no escritura.
		if (/reinterpret_cast\s*<\s*const/.test(line)) continue;
		// Escrituras claras: relleno por llamada, copia, o índice asignado.
		const clear_write =
			/(\bmemcpy\b|\bmemset\b|\bmemclr\b|\.fill\(|view\.data\(\)\s*\[[^\]]*\]\s*=[^=])/.test(line);
		if (!clear_write) {
			// `= ... view.data()` solo cuenta si el destino NO es `const` (una dirección
			// constante para BPLxPT/atributos es lectura, no escritura).
			if (!/=\s*[^;]*view\.data\(\)/.test(line)) continue;
			if (/\bconst\b/.test(line.split('=')[0])) continue;
		}
		out.push(i + 1);
	}
	return out;
}

const rel = (p) => path.relative(ROOT, p).replace(/\\/g, '/');
const offenders = new Map(); // relPath -> [lineas]
for (const top of SCAN_DIRS) {
	for (const file of listSources(path.join(ROOT, top))) {
		const text = fs.readFileSync(file, 'utf8');
		const lines = write_lines(text);
		if (lines.length) offenders.set(rel(file), lines);
	}
}
const files = [...offenders.keys()].sort();

function loadSet(p) {
	if (!fs.existsSync(p)) return new Set();
	return new Set(
		fs.readFileSync(p, 'utf8')
			.split('\n')
			.map((l) => l.trim())
			.filter((l) => l && !l.startsWith('#'))
			.map((l) => l.split(/\s+/)[0]),
	);
}
const frontier = loadSet(FRONTIER_PATH);
const baseline = loadSet(BASELINE_PATH);
const args = process.argv.slice(2);

if (args.includes('--list')) {
	for (const f of files) {
		const tag = frontier.has(f) ? 'FRONTERA' : baseline.has(f) ? 'baseline' : 'NUEVO   ';
		console.log(`${tag} ${f} (${offenders.get(f).length})`);
	}
	console.log(`[demo-raw-writes] ${files.length} fichero(s) con escritura directa.`);
	process.exit(0);
}
if (args.includes('--update-baseline')) {
	const lines = [
		'# Baseline del gate demo-raw-writes (ver tools/check/demo-raw-writes.mjs):',
		'# ficheros con escritura directa por view.data() pendientes de migrar a primitivas',
		'# del engine. NO-REINCIDENCIA: solo puede encoger.',
	];
	for (const f of files) {
		if (!frontier.has(f)) lines.push(f);
	}
	fs.writeFileSync(BASELINE_PATH, lines.join('\n') + '\n');
	console.log(`[demo-raw-writes] baseline actualizado (${files.length - frontier.size} ficheros)`);
	process.exit(0);
}

const violations = files.filter((f) => !frontier.has(f) && !baseline.has(f));
if (violations.length) {
	for (const f of violations) {
		console.error(`[demo-raw-writes] FALLO: ${f} escribe por view.data() sin declarar (${offenders.get(f).join(', ')})`);
	}
	console.error('[demo-raw-writes] declara la frontera (demo-raw-frontier.txt) o usa la primitiva del engine.');
	process.exit(1);
}
console.log(
	`[demo-raw-writes] OK: ${files.length} fichero(s); ${frontier.size} frontera declarada, ${baseline.size} en baseline.`,
);
