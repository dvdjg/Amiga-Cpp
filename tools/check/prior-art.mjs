// ---------------------------------------------------------------------------
// Gate de "arte previo" (prior-art): impide repetir el fallo de reimplementar a
// bajo nivel algo que el engine YA abstrae sin, al menos, declarar que se buscó.
//
// Idea: una regla escrita en un doc se ignora; este checker la convierte en un
// gate mecánico de la suite:
//
//   1. Si una demo/juego usa un PATRÓN de bajo nivel de una regla
//      (`tools/check/prior-art-rules.txt`), su `README.md` debe tener una
//      sección "## Búsqueda previa".
//   2. Esa sección debe citar el patrón usado y al menos un SÍMBOLO CANDIDATO
//      de la regla, y ese símbolo debe EXISTIR en `engine/include` (se verifica
//      por grep: no vale inventarse la declaración).
//   3. Las demos históricas se aceptan en `prior-art-baseline.txt` (solo puede
//      encoger); cualquier demo nueva que use el patrón sin declarar falla.
//
// Uso:
//   node tools/check/prior-art.mjs            # gate (falla si hay violaciones nuevas)
//   node tools/check/prior-art.mjs --list     # qué demos usan cada regla
//   node tools/check/prior-art.mjs --update-baseline
//
// Formato de reglas (TSV, `#` comenta):
//   id <TAB> descripción <TAB> patrones(;regex) <TAB> candidatos(;símbolo) <TAB> doc
// ---------------------------------------------------------------------------
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const RULES_PATH = path.join(ROOT, 'tools/check/prior-art-rules.txt');
const BASELINE_PATH = path.join(ROOT, 'tools/check/prior-art-baseline.txt');
const SCAN_DIRS = ['demos', 'games'];
const MARKER = /^##+\s*b[uú]squeda previa/i;

function loadRules() {
	const text = fs.readFileSync(RULES_PATH, 'utf8');
	const rules = [];
	for (const line of text.split('\n')) {
		const t = line.trim();
		if (t === '' || t.startsWith('#')) continue;
		const [id, desc, patterns, candidates, doc] = t.split('\t');
		if (!id || !patterns || !candidates) continue;
		rules.push({
			id: id.trim(),
			desc: (desc || '').trim(),
			patterns: patterns.split(';').map((p) => p.trim()).filter(Boolean),
			candidates: candidates.split(';').map((c) => c.trim()).filter(Boolean),
			doc: (doc || '').trim(),
		});
	}
	return rules;
}

/// ¿Existe `symbol` en alguna cabecera del engine? (la declaración no se inventa)
function symbolExists(symbol) {
	const needle = symbol.replace(/.*\//, '');
	const roots = [path.join(ROOT, 'engine/include')];
	const walk = (dir) => {
		for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
			const p = path.join(dir, e.name);
			if (e.isDirectory()) {
				if (walk(p)) return true;
			} else if (/\.(hpp|h)$/.test(e.name)) {
				if (fs.readFileSync(p, 'utf8').includes(needle)) return true;
			}
		}
		return false;
	};
	return roots.some(walk);
}

function listDemoSources(demoDir) {
	const out = [];
	const walk = (dir) => {
		for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
			const p = path.join(dir, e.name);
			if (e.isDirectory()) {
				if (e.name === 'vision' || e.name === 'out') continue;
				walk(p);
			} else if (/\.(cpp|hpp|h)$/.test(e.name)) {
				out.push(p);
			}
		}
	};
	walk(path.join(demoDir, 'src'));
	return out;
}

function demoDirOf(file) {
	// demos/<...>/NNN_tema/src/... -> demos/<...>/NNN_tema
	let d = path.dirname(file);
	while (!/^[0-9]{3}_/.test(path.basename(d)) && path.dirname(d) !== d) {
		d = path.dirname(d);
	}
	return d;
}

/// Extrae el texto de la sección "Búsqueda previa" del README de la demo.
function searchDeclaration(demoDir) {
	const readme = path.join(demoDir, 'README.md');
	if (!fs.existsSync(readme)) return null;
	const lines = fs.readFileSync(readme, 'utf8').split('\n');
	let start = -1;
	for (let i = 0; i < lines.length; i++) {
		if (MARKER.test(lines[i])) {
			start = i + 1;
			break;
		}
	}
	if (start < 0) return null;
	let end = lines.length;
	for (let i = start; i < lines.length; i++) {
		if (/^##\s/.test(lines[i])) {
			end = i;
			break;
		}
	}
	return lines.slice(start, end).join('\n');
}

const args = process.argv.slice(2);
const rules = loadRules();

// Recolecta demos y sus fuentes una vez.
const demos = new Map(); // demoDir -> {sources: Map<file,text>, text}
for (const top of SCAN_DIRS) {
	const base = path.join(ROOT, top);
	if (!fs.existsSync(base)) continue;
	const walk = (dir) => {
		for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
			const p = path.join(dir, e.name);
			if (e.isDirectory()) {
				walk(p);
			} else if (/\.(cpp|hpp)$/.test(e.name) && p.includes(`${path.sep}src${path.sep}`)) {
				const demo = demoDirOf(p);
				if (!demos.has(demo)) demos.set(demo, { text: '' });
				demos.get(demo).text += fs.readFileSync(p, 'utf8');
			}
		}
	};
	walk(base);
}

const rel = (p) => path.relative(ROOT, p).replace(/\\/g, '/');
const baseline = fs.existsSync(BASELINE_PATH)
	? new Set(fs.readFileSync(BASELINE_PATH, 'utf8').split('\n').map((l) => l.trim()).filter((l) => l && !l.startsWith('#')))
	: new Set();

const violations = [];
const rows = [];
for (const [demo, info] of [...demos.entries()].sort()) {
	for (const rule of rules) {
		const used = rule.patterns.filter((p) => info.text.includes(p));
		if (used.length === 0) continue;
		const key = `${rel(demo)}  ${rule.id}`;
		const decl = searchDeclaration(demo);
		let ok = false;
		let why = 'sin sección "## Búsqueda previa" en README.md';
		if (decl !== null) {
			const citesPattern = rule.patterns.some((p) => decl.includes(p));
			const cited = rule.candidates.filter((c) => decl.includes(c));
			const validCited = cited.filter((c) => symbolExists(c));
			if (!citesPattern) {
				why = 'la sección no cita el patrón de bajo nivel usado';
			} else if (validCited.length === 0) {
				why = `la sección no cita ningún candidato existente (${rule.candidates.join(', ')})`;
			} else {
				ok = true;
				why = `declarado (cita ${validCited.join(', ')})`;
			}
		}
		rows.push({ demo: rel(demo), rule, used, ok, why, key });
		if (!ok && !baseline.has(key)) violations.push({ demo: rel(demo), rule, used, why });
	}
}

if (args.includes('--list')) {
	for (const r of rows) {
		console.log(`${r.ok ? 'OK ' : 'MAL'} ${r.demo} [${r.rule.id}] usos=${r.used.join(',')} :: ${r.why}`);
	}
	process.exit(0);
}
if (args.includes('--update-baseline')) {
	const lines = [
		'# Baseline del gate prior-art: demos históricas que usan patrones de bajo nivel',
		'# sin declarar "Búsqueda previa" en su README. NO-REINCIDENCIA: solo puede encoger',
		'# (borra entradas al declarar la búsqueda o al migrar a la capa de alto nivel).',
	];
	for (const r of rows) {
		if (!r.ok) lines.push(r.key);
	}
	fs.writeFileSync(BASELINE_PATH, lines.join('\n') + '\n');
	console.log(`[prior-art] baseline actualizado (${rows.filter((r) => !r.ok).length} entradas)`);
	process.exit(0);
}

if (violations.length > 0) {
	for (const v of violations) {
		console.error(`[prior-art] FALLO: ${v.demo} usa ${v.used.join(', ')} (regla "${v.rule.id}": ${v.rule.desc})`);
		console.error(`            ${v.why}.`);
		console.error(`            El engine ya abstrae esto: revisa ${v.rule.doc} y declara la`);
		console.error('            decisión en el README de la demo bajo "## Búsqueda previa"');
		console.error(`            (cita el patrón y el candidato: ${v.rule.candidates.join(', ')}).`);
	}
	console.error(`[prior-art] ${violations.length} demo(s) sin declaración de arte previo.`);
	process.exit(1);
}
console.log('[prior-art] OK: los usos de bajo nivel están declarados o en baseline.');
