// Comprueba que las rutas de herramientas citadas en los documentos de arranque
// (AGENTS.md y los docs de entrada/operación) existan de verdad. Evita que un
// hilo nuevo siga instrucciones muertas (p. ej. `tools/foo.ps1` ya eliminado):
// links.mjs valida enlaces Markdown, pero no las rutas dentro de `code spans`.
//
// Alcance: solo prefijos operativos (tools/, scripts/, support/). Los documentos
// con rutas historicas a proposito (DEVELOPMENT_LOG.md y demas bitacoras) NO se
// revisan; la lista FILES es explicita a proposito.
//
// Uso: node tools/check/entrypoints.mjs [--quiet]
// Falla (exit 1) si una ruta citada no existe en el repo.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(__dirname, '../..');

// Documentos de arranque/operacion: lo que un hilo nuevo lee antes de trabajar.
const FILES = [
	'AGENTS.md',
	'docs/README.md',
	'docs/CONTINUATION_CONTEXT.md',
	'docs/ai-dev-environment/README.md',
	'docs/ai-dev-environment/DOC-MAP-PRINCIPAL.md',
	'docs/ai-dev-environment/AMIGA_DEV_GUIDE.md',
	'docs/debugging/system/setup-nuevo-equipo.md',
	'docs/build/BUILD_AND_RUN.md',
	'docs/guides/methodology/PROCEDIMIENTO_DEMOS_Y_JUEGOS.md',
];

// Prefijos operativos cuyo renombrado/borrado rompe instrucciones. Se exige que
// el prefijo no venga pegado a otro token (p. ej. `docs/tools/...` o
// `host-tools/...` no cuentan como `tools/...`), pero si se admite el prefijo
// explicito `./` / `.\` (patron habitual de comandos en la documentacion).
const PATH_RE = /(?<![\w./\\-])(?:\.\.?[\\/])?(?:tools|scripts|support)[\\/][\w.\-/\\]+/g;

const QUIET = process.argv.includes('--quiet');
if (process.argv.includes('--help') || process.argv.includes('-h')) {
	console.log(`Comprueba que las rutas operativas citadas en los documentos de arranque existan.

Uso: node tools/check/entrypoints.mjs [--quiet] [--help]

  Revisa las rutas con prefijo tools/, scripts/ o support/ (en \`code spans\` o
  texto) de los documentos de arranque (AGENTS.md y los docs de entrada). Falla
  si alguna no existe: es la senal de una instruccion obsoleta.

  --quiet  solo resume y fallos.
  --help   muestra esta ayuda.`);
	process.exit(0);
}

const problems = [];
const seen = new Set();

// SECCIONES CANONICAS de AGENTS.md: un truncado del documento de arranque (edicion
// scriptada que pierde el resto del fichero, merge mal resuelto...) debe FALLAR aqui,
// no descubrirse leyendo. Si se elimina una seccion a proposito, se quita tambien de
// esta lista en el mismo commit (acto deliberado y revisable).
const AGENTS_SECTIONS = [
	'## 0.',
	'### 1.1',
	'### 1.2',
	'### 1.3',
	'### 1.4',
	'### 1.5',
	'### 1.6',
	'### 1.7',
	'### 1.8',
	'### 1.9',
	'### 1.10',
	'### 1.11',
	'### 1.12',
	'### 1.13',
	'### 1.14',
	'### 1.15',
	'### 1.16',
	'## 2.',
	'### 2.1',
	'## 3.',
	'### 3.1',
	'### 3.2',
	'### 3.3',
	'### 3.4',
	'## 4.',
	'## 5. Cierre de turno',
];
{
	const agentsPath = path.join(ROOT, 'AGENTS.md');
	if (fs.existsSync(agentsPath)) {
		const agents = fs.readFileSync(agentsPath, 'utf8');
		for (const section of AGENTS_SECTIONS) {
			if (!agents.includes(section)) {
				problems.push(`AGENTS.md: falta la seccion "${section}" (truncado o edicion incompleta)`);
			}
		}
	}
}

function existsRepoOrRelative(file, target) {
	// La cita puede ser relativa a la raiz del repo (habitual) o al propio
	// documento (enlaces Markdown como `tools/README.md` desde docs/).
	return (
		fs.existsSync(path.join(ROOT, target)) ||
		fs.existsSync(path.resolve(path.dirname(file), target))
	);
}

for (const rel of FILES) {
	const file = path.join(ROOT, rel);
	if (!fs.existsSync(file)) {
		problems.push(`${rel}: no existe (revisa la lista FILES de este check)`);
		continue;
	}
	const lines = fs.readFileSync(file, 'utf8').split(/\r?\n/);
	for (let i = 0; i < lines.length; i++) {
		for (const m of lines[i].matchAll(PATH_RE)) {
			// Normaliza separadores de Windows y puntuacion final.
			let target = m[0].replace(/\\/g, '/').replace(/[.,;:!?)"'\`]+$/, '');
			if (!target || target.endsWith('/')) target = target.replace(/\/+$/, '');
			if (!target || target === 'tools' || target === 'scripts' || target === 'support') continue;
			if (existsRepoOrRelative(file, target)) continue;
			const key = `${rel}:${i + 1}:${target}`;
			if (seen.has(key)) continue;
			seen.add(key);
			problems.push(key + ' no existe');
		}
	}
}

if (problems.length) {
	for (const p of problems) console.error(`[entrypoints] FAIL: ${p}`);
	console.error(`[entrypoints] ${problems.length} ruta(s) citadas en docs de arranque que no existen.`);
	process.exit(1);
}
if (!QUIET) console.log('[entrypoints] OK: rutas de los docs de arranque existentes.');
