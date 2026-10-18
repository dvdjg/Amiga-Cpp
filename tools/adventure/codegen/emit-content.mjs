#!/usr/bin/env node
// Genera el **contenido C++ del engine** (`eng::adv`) a partir de un SIM v2 decodificado:
// tablas `constexpr` de walkboxes y hotspots por sala + índice de salas con `find_room()`.
// Cierra el ciclo del pipeline: extracción → SIM v2 → **tables del engine** (docs/engine/
// architecture/ADVENTURE_LAYER.md; `engine/include/eng/adv/`).
//
// Determinista (mismo SIM → mismos bytes) y regenerable: el header resultante es salida
// cocinada del workspace (`cook/cpp/`), nunca se edita a mano.
//
// Uso: node tools/adventure/codegen/emit-content.mjs <juego|sim.json> [--out <header.hpp>] [--namespace <ns>]

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { writeDirReadme } from '../lib/docs.mjs';

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
	console.error('Uso: node tools/adventure/codegen/emit-content.mjs <juego|sim.json> [--out <header.hpp>] [--namespace <ns>]');
	process.exit(2);
}

const ws = path.join(REPO, 'out', 'adventure', target);
const targetPath = path.resolve(target);
const simFile = fs.existsSync(path.join(ws, 'decoded', 'sim.json'))
	? path.join(ws, 'decoded', 'sim.json')
	: fs.existsSync(targetPath) && fs.statSync(targetPath).isDirectory()
		? path.join(targetPath, 'decoded', 'sim.json')
		: targetPath;
const sim = JSON.parse(fs.readFileSync(simFile, 'utf8'));
if (sim.format !== 'sim/2') {
	console.error(`[codegen] se esperaba un SIM v2 (es ${sim.format}); decodifica antes con tools/adventure/decode/`);
	process.exit(1);
}
const GAME = sim.game?.id ?? target;
const NS = (args.namespace ?? GAME).replace(/[^A-Za-z0-9_]/g, '_');

const cComment = (s) => String(s).replace(/[\\"]/g, '').replace(/[\r\n]+/g, ' ');

// `Vec<2,S>` es `{ S v[2] }`: un punto se inicializa `{{x, y}}` y cada elemento de las tablas
// lleva sus propias llaves exteriores (sin elisión implícita).
const pt = (v) => `{{${v[0]}, ${v[1]}}}`;

const walkboxLine = (b) => {
	const scale = Math.min(b.scale ?? 255, 255);
	return `\t{ ${pt(b.ul)}, ${pt(b.ur)}, ${pt(b.lr)}, ${pt(b.ll)}, 0x${b.mask.toString(16).padStart(2, '0')}, ` +
		`0x${b.flags.toString(16).padStart(2, '0')}, ${scale} },`;
};

const hotspotLine = (o) => {
	// Rect inclusivo: min = (x, y), max = (x + w - 1, y + h - 1) — el mismo criterio que el
	// hit-test del original (`x <= px < x + w`).
	const x0 = o.x, y0 = o.y, x1 = o.x + Math.max(o.width, 1) - 1, y1 = o.y + Math.max(o.height, 1) - 1;
	return `\t{ {{${x0}, ${y0}}}, {{${x1}, ${y1}}}, ${o.id}u },`;
};

const lines = [];
lines.push('// GENERADO por tools/adventure/codegen/emit-content.mjs — NO editar a mano.');
lines.push(`// Fuente: ${path.relative(REPO, simFile).replace(/\\/g, '/')} (SIM v2, motor ${sim.game?.engine ?? '?'}).`);
lines.push('// Regenerar: node tools/adventure/codegen/emit-content.mjs ' + GAME);
lines.push('#pragma once');
lines.push('');
lines.push('#include <eng/adv/room.hpp>');
lines.push('');
lines.push(`namespace ${NS} {`);
lines.push('');
lines.push('namespace content {');
lines.push('');

const rooms = (sim.rooms ?? []).filter((r) => (r.walkboxes?.length ?? 0) > 0 || (r.objects?.length ?? 0) > 0);
for (const r of rooms) {
	const note = `Sala ${r.room} «${cComment(r.name)}»: ${r.width}x${r.height}, ` +
		`${r.walkboxes.length} walkboxes, ${r.objects.length} hotspots.`;
	if (r.walkboxes.length > 0) {
		lines.push(`/// ${note}`);
		lines.push(`inline constexpr eng::adv::Walkbox<eng::s16> kRoom${r.room}Walkboxes[] = {`);
		for (const b of r.walkboxes) lines.push(walkboxLine(b));
		lines.push('};');
		lines.push('');
	}
	if (r.objects.length > 0) {
		lines.push(`/// Hotspots de la ${note.charAt(0).toLowerCase()}${note.slice(1)}`);
		lines.push(`inline constexpr eng::adv::Hotspot<eng::s16> kRoom${r.room}Hotspots[] = {`);
		for (const o of r.objects) lines.push(hotspotLine(o));
		lines.push('};');
		lines.push('');
	}
}
lines.push(`/// Índice de salas con contenido (${rooms.length} de ${(sim.rooms ?? []).length}); lookup lineal.`);
lines.push('inline constexpr eng::adv::RoomContent<eng::s16> kRooms[] = {');
for (const r of rooms) {
	const walk = r.walkboxes.length > 0 ? `kRoom${r.room}Walkboxes` : '{}';
	const hot = r.objects.length > 0 ? `kRoom${r.room}Hotspots` : '{}';
	lines.push(`\t{${r.room}u, ${r.width}u, ${r.height}u, ${walk}, ${hot}},`);
}
lines.push('};');
lines.push('');
lines.push('/// Busca el contenido de una sala por id (escaneo lineal; son decenas de salas).');
lines.push('/// \\param id id de sala (SIM `room`).');
lines.push('/// \\return puntero al contenido, o `nullptr` si el juego no tiene esa sala.');
lines.push('[[nodiscard]] constexpr const eng::adv::RoomContent<eng::s16>* find_room(eng::adv::RoomId id) noexcept {');
lines.push('\tfor (const eng::adv::RoomContent<eng::s16>& room : kRooms) {');
lines.push('\t\tif (room.id == id) return &room;');
lines.push('\t}');
lines.push('\treturn nullptr;');
lines.push('}');
lines.push('');
lines.push('} // namespace content');
lines.push('');
lines.push(`} // namespace ${NS}`);
lines.push('');

const outFile = args.out ? String(args.out) : path.join(ws, 'cook', 'cpp', `${GAME}_content.hpp`);
fs.mkdirSync(path.dirname(outFile), { recursive: true });
fs.writeFileSync(outFile, lines.join('\n'));
writeDirReadme(path.dirname(outFile), {
	title: `Contenido C++ para el engine (${GAME})`,
	what: 'Tablas `constexpr` de **contenido de aventura** para `eng::adv`: walkboxes y hotspots por sala, más el índice `kRooms` con `find_room()`. Generado desde el SIM v2; es lo que compila el juego.',
	producedBy: '`node tools/adventure/codegen/emit-content.mjs <juego>` (base: `decoded/sim.json`).',
	consumedBy: 'el juego/demo en el engine (incluir el header con `-I out/adventure/<juego>/cook/cpp`); validado compilando el contenido de MI1 contra el engine host.',
	items: [{ name: `${GAME}_content.hpp`, desc: 'namespace `<juego>::content`: arrays de `Walkbox<s16>`/`Hotspot<s16>` por sala + `kRooms[]` + `find_room(id)`.' }],
	related: [
		{ name: '`../../decoded/sim.json`', desc: 'fuente de verdad: cada caja/hotspot sale de `rooms[]` (y de los PNG para la parte visual).' },
		{ name: '`engine/include/eng/adv/`', desc: 'tipos destino (`Walkbox`, `Hotspot`, `RoomContent`); HOST-473 los prueba.' },
	],
	notes: ['No editar a mano: se regenera. Los cambios de contenido se hacen en `edited/`/SIM y se vuelve a emitir.'],
});
console.log(`[codegen] ${GAME}: ${rooms.length} salas con contenido -> ${outFile}`);
