// Gate de ESTILO: **punteros crudos a OBJETO** en las cabeceras del engine. La regla
// (`docs/engine/architecture/CODING_STYLE.md` §"Punteros y tipos") es: un observador no
// propietario se expresa con `eng::Ref<T>`/`eng::NonNull<T>`; un buffer, con `eng::Span<T>`
// (`Span<u8>`/`Span<u16>`…); un callback, con una **política de plantilla** (no `void*`+función).
// El `T*` crudo queda solo para memoria cruda (buffers `u8*`/`u16*`/`s16*`…), y siempre citando el
// motivo.
//
// Detecta (`Foo` = tipo de objeto, no escalar):
//   - miembros/estáticos:  `Foo* m_x` / `s_x` / `g_x`
//   - parámetros/retorno:  `Foo* name)` / `Foo* name,` / `Foo* name(`
//   - locales:             `Foo* name = ...` / `Foo* name;`
// Candidatos existentes (triage) en `raw-pointer-members-baseline.txt`; cualquier NUEVO falla.
// Los escalares (`ALLOW_TYPE`) y los tipos con nombre en `OWNERSHIP_TYPE` (Buffers/views/blocks
// que no son observadores) se aceptan.
//
// Uso: node tools/check/raw-pointer-members.mjs [--update-baseline | --report | --dump [dir]]
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(__dirname, '../..');
const ENG = path.join(ROOT, 'engine/include/eng');
const BASELINE = path.join(__dirname, 'raw-pointer-members-baseline.txt');

// Tipos escalares: un `u8*`/`s16*`… es memoria cruda, no un observador de objeto.
const ALLOW_TYPE =
	/^(std::FILE|FILE|T|A|K|V|U|S|word_t|u8|u16|u32|s8|s16|s32|char|void|usize|uintptr)$/;
// Tipos de propiedad/vista que NO son observadores (no se envuelven en `Ref`).
const OWNERSHIP_TYPE = /^(Span|PlaneBytes|MaskBytes|Bytes|Stream|Block|WordView|ByteView|View)$/;

function walk(dir) {
	const out = [];
	for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
		const p = path.join(dir, e.name);
		if (e.isDirectory()) out.push(...walk(p));
		else if (e.name.endsWith('.hpp')) out.push(p);
	}
	return out;
}

const TYPE = String.raw`([A-Za-z_][A-Za-z0-9_:]*(?:<[^<>]*>)?)`;
const NAME = String.raw`([A-Za-z_][A-Za-z0-9_]*)`;
const PATTERNS = [
	// miembro / estático / global
	new RegExp(String.raw`\b${TYPE}\*\s+([msg]_[a-z][A-Za-z0-9_]*)\b`),
	// parámetro o retorno de función
	new RegExp(String.raw`\b${TYPE}\*\s+${NAME}\s*[,)(]`),
	// variable local declarada (inicializada o cerrada con `;`)
	new RegExp(String.raw`\b${TYPE}\*\s+${NAME}\s*[=;]`),
];

function typeOk(type) {
	const base = type.split('::').pop();
	return ALLOW_TYPE.test(base) || OWNERSHIP_TYPE.test(base);
}

const found = [];
for (const abs of walk(ENG)) {
	const rel = path.relative(ENG, abs).replace(/\\/g, '/');
	if (rel === 'core/ptr.hpp') continue;
	fs.readFileSync(abs, 'utf8').split(/\r?\n/).forEach((line, i) => {
		const t = line.trim();
		if (t.startsWith('//') || t.startsWith('*') || t.startsWith('/*')) return;
		for (const re of PATTERNS) {
			const m = t.match(re);
			if (!m) continue;
			if (!typeOk(m[1]))
				found.push({ key: `eng/${rel}:${m[2]}`, rel, line: i + 1, text: t });
			break;
		}
	});
}
found.sort((a, b) => a.key.localeCompare(b.key));

// `--dump [dir]`: imprime los candidatos con fichero:línea para migrarlos uno a uno.
if (process.argv.includes('--dump')) {
	const filter = process.argv[process.argv.indexOf('--dump') + 1];
	const rows = filter && !filter.startsWith('--') ? found.filter((f) => f.rel.startsWith(filter)) : found;
	for (const f of rows) console.log(`eng/${f.rel}:${f.line}: ${f.text}`);
	console.log(`[raw-pointer-members] ${rows.length} candidato(s).`);
	process.exit(0);
}

// `--report`: lista la deuda agrupada por carpeta (para priorizar la migración a Ref/Span).
if (process.argv.includes('--report')) {
	const byDir = new Map();
	for (const f of found) {
		const dir = f.rel.split('/').slice(0, -1).join('/') || '.';
		byDir.set(dir, (byDir.get(dir) ?? 0) + 1);
	}
	const rows = [...byDir.entries()].sort((a, b) => b[1] - a[1] || a[0].localeCompare(b[0]));
	for (const [dir, n] of rows) {
		console.log(`  ${String(n).padStart(4)}  ${dir}`);
	}
	const members = found.filter((f) => /:[msg]_/.test(f.key));
	console.log(`[raw-pointer-members] deuda: ${found.length} (${members.length} miembros)`);
	for (const m of members) console.log(`    miembro: ${m.key}`);
	process.exit(0);
}

if (process.argv.includes('--update-baseline')) {
	const header =
		'# Baseline de `raw-pointer-members.mjs`: punteros crudos a OBJETO aceptados (DEUDA).\n' +
		'# Migrar a eng::Ref/NonNull (observador), eng::Span (buffer) o política de plantilla\n' +
		'# (callback). Al migrar una entrada, bórrala de aquí (`--update-baseline` la regenera).\n' +
		'# Ver docs/engine/architecture/CODING_STYLE.md §"Punteros y tipos de C".\n';
	const keys = found.map((f) => f.key);
	fs.writeFileSync(BASELINE, header + keys.join('\n') + (keys.length ? '\n' : ''), 'utf8');
	console.log(`[raw-pointer-members] baseline actualizada (${found.length} entradas).`);
	process.exit(0);
}

const baseline = new Set(
	(fs.existsSync(BASELINE) ? fs.readFileSync(BASELINE, 'utf8') : '')
		.split(/\r?\n/)
		.map((l) => l.trim())
		.filter((l) => l && !l.startsWith('#')),
);

const problems = found.filter((f) => !baseline.has(f.key));
if (problems.length) {
	for (const p of problems.slice(0, 40)) {
		console.error(`[raw-pointer-members] FAIL: ${p.key} (usa eng::Ref/NonNull o eng::Span)`);
	}
	console.error(
		`[raw-pointer-members] ${problems.length} puntero(s) a objeto no propietario sin justificar.`,
	);
	process.exit(1);
}
console.log('[raw-pointer-members] OK: sin punteros a objeto sin justificar (baseline aparte).');
