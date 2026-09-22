// Gate: los **punteros a objeto no propietarios en miembros** de cabeceras genéricas del
// engine deben ser `eng::Ref<T>`/`eng::NonNull<T>` (`eng/core/ptr.hpp`), no `T*`. Los
// candidatos actuales (triage) van en `raw-pointer-members-baseline.txt`: `:m_audio` =
// pendiente de migrar a `Ref`; `:m_file`/`:m_used`/`:m_cells`/`:m_tile_layers` = buffers de
// almacenamiento (se aceptan). Cualquier miembro NUEVO falla.
//
// Uso: node tools/check/raw-pointer-members.mjs [--update-baseline]
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(__dirname, '../..');
const ENG = path.join(ROOT, 'engine/include/eng');
const BASELINE = path.join(__dirname, 'raw-pointer-members-baseline.txt');

const EXEMPT_DIR = /(^|\/)(retro|platform|cpu|field)\//;
const ALLOW_TYPE = /^(std::FILE|FILE|T|A|K|V|U|S|u8|u16|u32|s8|s16|s32|char|void)$/;

function walk(dir) {
	const out = [];
	for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
		const p = path.join(dir, e.name);
		if (e.isDirectory()) out.push(...walk(p));
		else if (e.name.endsWith('.hpp')) out.push(p);
	}
	return out;
}

const found = [];
for (const abs of walk(ENG)) {
	const rel = path.relative(ENG, abs).replace(/\\/g, '/');
	if (EXEMPT_DIR.test(rel) || rel === 'core/ptr.hpp') continue;
	fs.readFileSync(abs, 'utf8').split(/\r?\n/).forEach((line) => {
		const t = line.trim();
		if (t.startsWith('//') || t.startsWith('*') || t.startsWith('/*')) return;
		// Cubre también tipos con plantilla de UN nivel (`Foo<T>* m_x`, p. ej. `ChunkStream<N>*`),
		// que antes se escapaban porque el tipo tenía `<...>`.
		const m = t.match(
			/\b([A-Za-z_][A-Za-z0-9_:]*(?:<[^<>]*>)?)\*\s+(_?m_[a-z][A-Za-z0-9_]*)\b/,
		);
		if (!m) return;
		const type = m[1].split('::').pop();
		if (ALLOW_TYPE.test(type)) return;
		found.push(`eng/${rel}:${m[2]}`);
	});
}
found.sort();

if (process.argv.includes('--update-baseline')) {
	fs.writeFileSync(BASELINE, found.join('\n') + (found.length ? '\n' : ''), 'utf8');
	console.log(`[raw-pointer-members] baseline actualizada (${found.length} entradas).`);
	process.exit(0);
}

const baseline = new Set(
	(fs.existsSync(BASELINE) ? fs.readFileSync(BASELINE, 'utf8') : '')
		.split(/\r?\n/)
		.map((l) => l.trim())
		.filter((l) => l && !l.startsWith('#')),
);

const problems = found.filter((f) => !baseline.has(f));
if (problems.length) {
	for (const p of problems) {
		console.error(`[raw-pointer-members] FAIL: ${p} (usa eng::Ref/NonNull para no-propietarios)`);
	}
	console.error(`[raw-pointer-members] ${problems.length} miembro(s) nuevo(s) con puntero a objeto no propietario.`);
	process.exit(1);
}
console.log('[raw-pointer-members] OK: sin punteros a objeto no propietarios nuevos (baseline aparte).');
