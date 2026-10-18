// ---------------------------------------------------------------------------
// Gate `md-wrap`: NO se admite el hard-wrap de párrafos en Markdown.
//
// Convención del repo (AGENTS §1.2): cada párrafo es UNA línea lógica; el editor
// ajusta con word wrap. Partir una frase a mitad de línea (~95 columnas) es un
// error de proceso: ensucia los diffs y no aporta nada.
//
// Detección: una línea seguida de otra NO estructural (no encabezado, tabla,
// ítem de lista nuevo, fence, línea en blanco, línea de referencia de enlace)
// cuya anterior no acaba en puntuación terminal → continuación a mitad de frase
// = hard-wrap. La continuaciones INDENTADAS de un ítem (dos o más espacios) también
// cuentan: deben fundirse en la línea del ítem.
//
// Exenciones: bloques ```...```, tablas (`|`), encabezados, diagramas ASCII
// (líneas que acaban en `\` o con dos espacios finales, y las que empiezan por
// caracteres de dibujo), y las líneas de referencia `[etiqueta]: url`.
//
// Baseline `md-wrap-baseline.txt` (un fichero por línea, solo puede encoger).
// Uso:  node tools/check/md-wrap.mjs [--list|--update-baseline]
// ---------------------------------------------------------------------------
import fs from 'node:fs';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const BASELINE_PATH = path.join(ROOT, 'tools/check/md-wrap-baseline.txt');

const isFence = (l) => /^\s*```/.test(l);
const isHeading = (l) => /^#{1,6}\s/.test(l);
const isTable = (l) => /^\s*\|/.test(l);
const isListStart = (l) => /^\s*([-*+]|\d+\.)\s+/.test(l);
const isIndentedCont = (l) => /^\s{2,}\S/.test(l) && !isListStart(l);
const isRefLine = (l) => /^\s*\[[^\]]+\]:\s/.test(l);
const isDiagram = (l) => /[\\|+\-]$/.test(l.trimEnd()) || / {2,}$/.test(l);
const endsTerminal = (l) => /[.:;!?)\]"'`»”…]$/.test(l.trimEnd());
const isHtmlComment = (l) => /^\s*<!--/.test(l);

/// Devuelve la lista de líneas (1-based) que parecen hard-wrap.
function findWraps(text) {
	const lines = text.split('\n');
	const bad = [];
	let inFence = false;
	for (let i = 0; i + 1 < lines.length; i++) {
		const cur = lines[i];
		if (isFence(cur)) { inFence = !inFence; continue; }
		if (inFence || cur.trim() === '') continue;
		if (isHeading(cur) || isTable(cur) || isRefLine(cur) || isHtmlComment(cur)) continue;
		if (isDiagram(cur)) continue;
		const nxt = lines[i + 1];
		if (nxt.trim() === '' || isFence(nxt) || isHeading(nxt) || isTable(nxt) || isRefLine(nxt)) continue;
		if (isListStart(nxt) && !isIndentedCont(nxt)) continue;
		if (endsTerminal(cur)) continue;
		bad.push(i + 1);
	}
	return bad;
}

function findMds() {
	const out = [];
	const walk = (dir) => {
		let entries;
		try { entries = fs.readdirSync(dir, { withFileTypes: true }); } catch { return; }
		for (const e of entries) {
			if (e.isDirectory()) {
				if (e.name === 'node_modules' || e.name === 'out' || e.name === 'obj' || e.name === 'dist') continue;
				walk(path.join(dir, e.name));
			} else if (e.name.endsWith('.md')) {
				out.push(path.join(dir, e.name));
			}
		}
	};
	for (const top of ['AGENTS.md', 'docs', 'demos', 'engine', 'tests', 'tools', 'games', 'assets', 'README.md']) {
		const p = path.join(ROOT, top);
		if (!fs.existsSync(p)) continue;
		const st = fs.statSync(p);
		if (st.isDirectory()) walk(p);
		else if (p.endsWith('.md')) out.push(p);
	}
	return out;
}

/// Filtra los ficheros **ignorados por git**: los artefactos generados (informes crudos de
/// vision, salidas de tools) no son documentacion editable y no deben romper el gate. Si git
/// no esta disponible, no se filtra (mejor el comportamiento estricto que el silencio).
function filterGitIgnored(files) {
	try {
		// Con `-z` la entrada tambien va separada por NUL (git lo exige) y la salida viene sin
		// comillar (con `--stdin` git C-quotea las rutas con backslashes en Windows).
		const input = files.map((f) => path.relative(ROOT, f)).join('\0');
		const res = execFileSync('git', ['check-ignore', '-z', '--stdin'], {
			cwd: ROOT, input, encoding: 'utf8', stdio: ['pipe', 'pipe', 'ignore'],
		});
		const ignored = new Set(res.split('\0').map((l) => l.trim()).filter(Boolean).map((l) => l.replace(/\\/g, '/')));
		return files.filter((f) => !ignored.has(path.relative(ROOT, f).replace(/\\/g, '/')));
	} catch {
		return files;
	}
}

const args = process.argv.slice(2);
const rel = (p) => path.relative(ROOT, p).replace(/\\/g, '/');
// Baseline con conteo: "<n> <ruta>" -> el numero de lineas wrapeadas SOLO puede bajar.
const baseline = new Map();
if (fs.existsSync(BASELINE_PATH)) {
	for (const l of fs.readFileSync(BASELINE_PATH, 'utf8').split('\n')) {
		const t = l.trim();
		if (t === '' || t.startsWith('#')) continue;
		const m = t.match(/^(\d+)\s+(.+)$/);
		if (m) baseline.set(m[2], parseInt(m[1], 10));
	}
}

const offenders = [];
for (const f of filterGitIgnored(findMds())) {
	const text = fs.readFileSync(f, 'utf8');
	const bad = findWraps(text);
	if (bad.length > 0) offenders.push({ file: rel(f), bad });
}

if (args.includes('--list')) {
	for (const o of offenders) {
		const base = baseline.get(o.file);
		const tag = base === undefined ? 'NUEVO' : (o.bad.length > base ? 'PEOR ' : 'base ');
		console.log(`${tag} ${o.file} :: ${o.bad.length} linea(s) (base ${base ?? '-'}), p.ej. ${o.bad.slice(0, 6).join(', ')}`);
	}
	process.exit(0);
}
if (args.includes('--update-baseline')) {
	const lines = [
		'# Baseline del gate md-wrap (ver tools/check/md-wrap.mjs): documentos con hard-wrap',
		'# historico. NO-REINCIDENCIA: al reflowar un documento, borra su linea.',
	];
	for (const o of offenders.sort((a, b) => a.file.localeCompare(b.file))) lines.push(`${String(o.bad.length).padStart(6)} ${o.file}`);
	fs.writeFileSync(BASELINE_PATH, lines.join('\n') + '\n');
	console.log(`[md-wrap] baseline actualizado (${offenders.length} ficheros)`);
	process.exit(0);
}

const violations = offenders.filter((o) => {
	const base = baseline.get(o.file);
	return base === undefined || o.bad.length > base;
});
if (violations.length > 0) {
	for (const v of violations) {
		console.error(`[md-wrap] FALLO: ${v.file}: ${v.bad.length} linea(s) con hard-wrap (base ${baseline.get(v.file) ?? 'nuevo'}; p. ej. ${v.bad.slice(0, 6).join(', ')})`);
	}
	console.error('[md-wrap] Une cada parrafo/item en UNA linea logica (AGENTS §1.2: sin limite de anchura;');
	console.error('          el editor ajusta con word wrap). Los bloques de codigo/tablas/diagramas no cuentan.');
	process.exit(1);
}
console.log(`[md-wrap] OK: ${filterGitIgnored(findMds()).length} .md; hard-wrap nuevo o creciente: no (baseline: ${baseline.size} ficheros historicos).`);
