#!/usr/bin/env node
// Comprueba que la logica de demo/juego usa la fachada `eng/api/api.hpp` y no nombra tipos
// del backend:
//   - no incluye headers cubiertos por la fachada (los trae ella);
//   - no usa `MinimalBackend::<tipo>` (debe usar el tipo de dominio o el seam).
// Regla: AGENTS.md 1.9. Uso: node tools/check/api-facade.mjs
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');

const COVERED = new Set([
	'eng/core/types/types.hpp', 'eng/core/types/domains.hpp', 'eng/core/types/box.hpp',
	'eng/debug/run_status.hpp', 'eng/engine.hpp', 'eng/field/draw_target.hpp',
	'eng/field/raster.hpp', 'eng/field/surface.hpp', 'eng/graphics/blitter_state.hpp',
	'eng/graphics/frame_plan.hpp', 'eng/graphics/palette32.hpp',
	'eng/graphics/composition/compose.hpp', 'eng/input/input.hpp', 'eng/memory/arena.hpp',
	'eng/scene/actor.hpp', 'eng/task/background.hpp',
]);

const stripComment = (l) => {
	const i = l.indexOf('//');
	return i >= 0 ? l.slice(0, i) : l;
};

function collect(dir, depth) {
	const out = [];
	const base = path.join(ROOT, dir);
	if (!fs.existsSync(base)) return out;
	const walk = (d, level) => {
		for (const e of fs.readdirSync(d, { withFileTypes: true })) {
			const p = path.join(d, e.name);
			if (e.isDirectory()) {
				if (level < depth) walk(p, level + 1);
			} else if (e.name.endsWith('.cpp') && p.includes(`${path.sep}src${path.sep}`)) {
				out.push(p);
			}
		}
	};
	walk(base, 0);
	return out;
}

const files = [...collect('demos', 3), ...collect('games', 2)];
const problems = [];
for (const f of files) {
	const rel = path.relative(ROOT, f).replaceAll('\\', '/');
	const lines = fs.readFileSync(f, 'utf8').split('\n');
	for (let i = 0; i < lines.length; i++) {
		const code = stripComment(lines[i]);
		const inc = code.match(/^#include <(eng\/[^>]+)>/);
		if (inc && COVERED.has(inc[1])) {
			problems.push(`${rel}:${i + 1}: incluye <${inc[1]}> (lo trae eng/api/api.hpp)`);
		}
		if (code.includes('MinimalBackend::') || code.includes('AmigaBackend::')) {
			problems.push(`${rel}:${i + 1}: usa un tipo del backend (MinimalBackend::/AmigaBackend::); usa el tipo de dominio o el seam`);
		}
	}
}

if (problems.length) {
	for (const p of problems) console.error(`[api-facade] AVISO: ${p}`);
	console.error(`[api-facade] ${problems.length} aviso(s): la logica de demo/juego debe usar la fachada (AGENTS 1.9).`);
	process.exit(1);
}
console.log(`[api-facade] OK: ${files.length} demos/juegos usan la fachada y no nombran tipos del backend.`);
