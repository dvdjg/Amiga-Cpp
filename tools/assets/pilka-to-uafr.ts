#!/usr/bin/env node
/**
 * Cocedor `obj2c` -> UAF-R (`MeshPoly`): convierte un asset `obj2c` (como `pilka.c`, el
 * icosaedro truncado de la demo 116) en un chunk de malla **n-gon** y lo empaqueta en un
 * `.uafr` consumible por `eng::assets::PolyMeshAssetView`.
 *
 * Es la forma amiga del raster (rellena polígonos convexos): la `pilka` queda en **32 caras**
 * en vez de los **116 triángulos** que saldrían al triangularla.
 *
 * Uso: node dist/tools/assets/pilka-to-uafr.js [salida.uafr]
 */
import fs from 'node:fs';
import path from 'node:path';
import { packUaf, polyMeshChunkData, UafChunkType } from './uaf-pack.js';

const ROOT = process.cwd(); // ejecutar desde la raíz del repo
const SRC = path.join(ROOT, 'demos/techniques/amiga/effects/116_flatshade_convex/src/data/pilka.c');
const OUT = process.argv[2] ?? path.join(ROOT, 'out/assets/mesh/pilka.uafr');

/** Extrae los enteros del inicializador de un array C `name[] = { ... };`. */
function initializerInts(text: string, name: string): number[] {
	const m = text.match(new RegExp(`${name}\\[\\]\\s*=\\s*\\{([\\s\\S]*?)\\};`));
	if (!m) throw new Error(`no se encontro el array ${name}[]`);
	return (m[1].match(/-?\d+/g) ?? []).map(Number);
}

/** Offset (bytes) y count de un `eng::Span<eng::s16>{ (s16*)((char*)BLOB + OFF), CNT }`. */
function groupSpan(
	structBody: string,
	blob: string,
	name: string,
): { off: number; count: number } {
	const re = new RegExp(
		`${name}\\s*=\\s*eng::Span<eng::s16>\\s*\\{\\s*` +
			`reinterpret_cast<eng::s16\\*>\\(\\(char\\*\\)${blob}\\s*\\+\\s*(\\d+)\\)\\s*,\\s*(\\d+)\\s*\\}`,
	);
	const m = structBody.match(re);
	if (!m) throw new Error(`no se encontro ${name} en el struct`);
	return { off: Number(m[1]), count: Number(m[2]) };
}

function convert() {
	// Sin comentarios: el formato `obj2c` documenta las secciones en `/* ... */`.
	const text = fs
		.readFileSync(SRC, 'utf8')
		.replace(/\/\*[\s\S]*?\*\//g, ' ')
		.replace(/\/\/[^\n]*/g, ' ');

	const structMatch = text.match(/Mesh3D\s+pilka\s*=\s*\{([\s\S]*?)\};/);
	if (!structMatch) throw new Error('no se encontro `Mesh3D pilka`');
	const body = structMatch[1];
	const nVerts = Number(body.match(/\.vertices\s*=\s*(\d+)/)?.[1]);
	const data = initializerInts(text, '_pilka_data');

	const vg = groupSpan(body, '_pilka_data', 'vertexGroups');
	const fg = groupSpan(body, '_pilka_data', 'faceGroups');

	// Vertices: layout `[flags, ox, oy, oz, x, y, z]` (7 s16); nos quedamos con (ox,oy,oz).
	const verts: number[][] = [];
	for (let i = 0; i < nVerts; i++) {
		const b = i * 7;
		verts.push([data[b + 1], data[b + 2], data[b + 3]]);
	}

	// El grupo lista los offsets de byte de cada vértice, en orden de índice.
	const indexOfOffset = new Map<number, number>();
	for (let k = 0, idx = 0; k < vg.count; k++) {
		const off = data[vg.off / 2 + k];
		if (off === 0) break;
		indexOfOffset.set(off, idx++);
	}

	// Caras: `[nx, ny, nz, flags:8|mat:8, count, (vert-off, edge-off)*count]`.
	const faces: number[][] = [];
	for (let k = 0; k < fg.count; k++) {
		const foff = data[fg.off / 2 + k];
		if (foff === 0) break;
		const fi = foff / 2;
		const count = data[fi + 4];
		const face: number[] = [];
		for (let j = 0; j < count; j++) {
			const voff = data[fi + 5 + j * 2];
			const vi = indexOfOffset.get(voff);
			if (vi === undefined) throw new Error(`offset de vértice desconocido: ${voff}`);
			face.push(vi);
		}
		faces.push(face);
	}

	const chunk = polyMeshChunkData(verts, faces);
	const blob = packUaf([{ type: UafChunkType.MeshPoly, count: faces.length, data: chunk }]);
	fs.mkdirSync(path.dirname(OUT), { recursive: true });
	fs.writeFileSync(OUT, blob);

	const tris = faces.reduce((a, f) => a + (f.length - 2), 0);
	console.log(
		`pilka: ${verts.length} vértices, ${faces.length} caras (n-gon), ${tris} triángulos ` +
			`-> ${path.relative(ROOT, OUT)} (${blob.length} B)`,
	);
	if (nVerts !== 60 || faces.length !== 32 || tris !== 116) {
		console.error('FAIL: topología inesperada (se esperaba 60/32/116)');
		process.exit(1);
	}
}

convert();
