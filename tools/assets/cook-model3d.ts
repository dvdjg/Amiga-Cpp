#!/usr/bin/env node
/**
 * Cooker host de `Model3D` (chunk 15 de UAF-R) para el engine Amiga.
 *
 * Lee un JSON de autoría **simple** (vértices, caras n-gon y materiales planos)
 * y emite un UAF-R con el chunk `Model3D` v1 que consume
 * `eng::assets::Model3DAssetView` (`engine/include/eng/assets/model3d.hpp`).
 *
 * El formato de runtime es big-endian y de capacidad fija:
 *
 *   Model3D payload (v1, 28 B de cabecera)
 *     u16 version=1, u16 flags, u16 vertex_count, u16 face_count,
 *     u16 index_count, u16 material_count,
 *     u32 vertex_offset, u32 index_offset, u32 face_offset, u32 material_offset
 *     vertex[]   { s16 x, s16 y, s16 z }
 *     index[]    { u16 }
 *     face[]     { u16 first, u16 count, u16 material }
 *     material[] { u8 color, u8 pattern, u16 flags }
 *
 * El JSON de ejemplo está en `MODEL3D_ASSET_FORMAT.md`; esta primera versión
 * cubre la malla estática + materiales. Nodos, clips y proxies (chunks 18/19)
 * se añadirán en las fases siguientes del roadmap.
 *
 * Uso:
 *   node dist/tools/assets/cook-model3d.js <model.json> <out.uafr>
 *   node dist/tools/assets/cook-model3d.js --self-test
 */
import * as fs from 'fs';
import { pathToFileURL } from 'url';
import { packUaf, parseUaf, UafChunkType } from './uaf-pack.js';

export const MODEL3D_HEADER_BYTES = 28;
export const MODEL3D_VERSION = 1;
export const MODEL3D_MAX_FACE_VERTICES = 8;

export interface ModelMaterialSource {
	color: number;
	pattern?: number;
	flags?: number;
}

export interface ModelFaceSource {
	vertices: number[];
	material?: number;
}

export interface ModelSource {
	format?: string;
	version?: number;
	vertices: number[][];
	faces: ModelFaceSource[];
	materials: ModelMaterialSource[];
}

interface Face {
	first: number;
	count: number;
	material: number;
}

function fail(msg: string): never {
	throw new Error(`cook-model3d: ${msg}`);
}

function byte(value: number, what: string): number {
	if (!Number.isInteger(value) || value < 0 || value > 255) {
		fail(`${what} debe ser un entero 0..255 (recibido ${value})`);
	}
	return value;
}

function word(value: number, what: string): number {
	if (!Number.isInteger(value) || value < 0 || value > 0xffff) {
		fail(`${what} debe ser un entero 0..65535 (recibido ${value})`);
	}
	return value;
}

function s16(value: number, what: string): number {
	if (!Number.isFinite(value) || value < -32768 || value > 32767) {
		fail(`${what} fuera de rango s16 (${value})`);
	}
	return Math.round(value);
}

/** Valida que una cara sea convexa y no degenerada (proyección 2D por el eje dominante). */
function validateConvexFace(vertices: number[][], face: ModelFaceSource, fi: number): void {
	if (!Array.isArray(face.vertices) || face.vertices.length < 3 || face.vertices.length > MODEL3D_MAX_FACE_VERTICES) {
		fail(`cara ${fi}: se esperan 3..${MODEL3D_MAX_FACE_VERTICES} vértices`);
	}
	const ids = face.vertices;
	for (const id of ids) {
		if (!Number.isInteger(id) || id < 0 || id >= vertices.length) {
			fail(`cara ${fi}: índice de vértice fuera de rango (${id})`);
		}
	}
	// Newell normal (sin normalizar).
	let nx = 0, ny = 0, nz = 0;
	for (let i = 0; i < ids.length; i++) {
		const a = vertices[ids[i]];
		const b = vertices[ids[(i + 1) % ids.length]];
		nx += (a[1] - b[1]) * (a[2] + b[2]);
		ny += (a[2] - b[2]) * (a[0] + b[0]);
		nz += (a[0] - b[0]) * (a[1] + b[1]);
	}
	const ax = Math.abs(nx), ay = Math.abs(ny), az = Math.abs(nz);
	if (ax + ay + az === 0) fail(`cara ${fi}: degenerada (normal cero)`);
	const drop = ax >= ay && ax >= az ? 0 : (ay >= az ? 1 : 2);
	const keep = [0, 1, 2].filter((k) => k !== drop);
	let sign = 0;
	for (let i = 0; i < ids.length; i++) {
		const a = vertices[ids[i]];
		const b = vertices[ids[(i + 1) % ids.length]];
		const c = vertices[ids[(i + 2) % ids.length]];
		const e1x = b[keep[0]] - a[keep[0]];
		const e1y = b[keep[1]] - a[keep[1]];
		const e2x = c[keep[0]] - b[keep[0]];
		const e2y = c[keep[1]] - b[keep[1]];
		const cross = e1x * e2y - e1y * e2x;
		if (cross === 0) continue;
		const s = Math.sign(cross);
		if (sign === 0) sign = s;
		else if (s !== sign) fail(`cara ${fi}: no convexa (giro inconsistente)`);
	}
	if (sign === 0) fail(`cara ${fi}: degenerada (sin área)`);
}

/** Serializa el payload `Model3D` v1 (big-endian). */
export function model3dChunkData(src: ModelSource): Buffer {
	if (!Array.isArray(src.vertices) || src.vertices.length === 0) fail('vertices vacío');
	if (src.vertices.length > 0xffff) fail('demasiados vértices');
	if (!Array.isArray(src.faces) || src.faces.length === 0) fail('faces vacío');
	if (src.faces.length > 0xffff) fail('demasiadas caras');
	if (!Array.isArray(src.materials) || src.materials.length === 0) fail('materials vacío');
	if (src.materials.length > 0xffff) fail('demasiados materiales');

	let indexCount = 0;
	for (let i = 0; i < src.faces.length; i++) {
		validateConvexFace(src.vertices, src.faces[i], i);
		indexCount += src.faces[i].vertices.length;
	}
	if (indexCount > 0xffff) fail('demasiados índices');

	const vertexBytes = src.vertices.length * 6;
	const indexBytes = indexCount * 2;
	const faceBytes = src.faces.length * 6;
	const materialBytes = src.materials.length * 4;
	const vOff = MODEL3D_HEADER_BYTES;
	const iOff = vOff + vertexBytes;
	const fOff = iOff + indexBytes;
	const mOff = fOff + faceBytes;
	const total = mOff + materialBytes;
	const out = Buffer.alloc(total);

	out.writeUInt16BE(MODEL3D_VERSION, 0);
	out.writeUInt16BE(0, 2);
	out.writeUInt16BE(src.vertices.length, 4);
	out.writeUInt16BE(src.faces.length, 6);
	out.writeUInt16BE(indexCount, 8);
	out.writeUInt16BE(src.materials.length, 10);
	out.writeUInt32BE(vOff, 12);
	out.writeUInt32BE(iOff, 16);
	out.writeUInt32BE(fOff, 20);
	out.writeUInt32BE(mOff, 24);

	let vo = vOff;
	for (let i = 0; i < src.vertices.length; i++) {
		const v = src.vertices[i];
		if (!Array.isArray(v) || v.length !== 3) fail(`vértice ${i}: se esperan 3 componentes`);
		out.writeInt16BE(s16(v[0], `vértice ${i}.x`), vo);
		out.writeInt16BE(s16(v[1], `vértice ${i}.y`), vo + 2);
		out.writeInt16BE(s16(v[2], `vértice ${i}.z`), vo + 4);
		vo += 6;
	}

	let io = iOff;
	for (let fi = 0; fi < src.faces.length; fi++) {
		const ids = src.faces[fi].vertices;
		for (const id of ids) {
			out.writeUInt16BE(word(id, `cara ${fi} índice`), io);
			io += 2;
		}
	}

	let fo = fOff;
	let first = 0;
	for (let fi = 0; fi < src.faces.length; fi++) {
		const face = src.faces[fi];
		const material = face.material ?? 0;
		if (material < 0 || material >= src.materials.length) {
			fail(`cara ${fi}: material fuera de rango (${material})`);
		}
		out.writeUInt16BE(first, fo);
		out.writeUInt16BE(face.vertices.length, fo + 2);
		out.writeUInt16BE(word(material, `cara ${fi} material`), fo + 4);
		first += face.vertices.length;
		fo += 6;
	}

	let mo = mOff;
	for (let mi = 0; mi < src.materials.length; mi++) {
		const m = src.materials[mi] ?? { color: 0 };
		out[mo + 0] = byte(m.color ?? 0, `material ${mi}.color`);
		out[mo + 1] = byte(m.pattern ?? 0, `material ${mi}.pattern`);
		out.writeUInt16BE(word(m.flags ?? 0, `material ${mi}.flags`), mo + 2);
		mo += 4;
	}
	return out;
}

/** Cocina el JSON de autoría a un blob UAF-R (un solo chunk `Model3D`). */
export function cookModel3d(src: ModelSource): Buffer {
	const payload = model3dChunkData(src);
	return packUaf([{ type: UafChunkType.Model3D, count: 1, data: payload }]);
}

function buildCubeSource(): ModelSource {
	const vertices: number[][] = [
		[-20, -20, -20], [20, -20, -20], [20, 20, -20], [-20, 20, -20],
		[-20, -20, 20], [20, -20, 20], [20, 20, 20], [-20, 20, 20],
	];
	const quads = [
		[4, 5, 6, 7], [0, 3, 2, 1], [7, 6, 2, 3],
		[0, 1, 5, 4], [1, 2, 6, 5], [0, 4, 7, 3],
	];
	const faces: ModelFaceSource[] = quads.map((q, i) => ({ vertices: q, material: i < 3 ? 0 : 1 }));
	return {
		format: 'amiga-model-source',
		version: 1,
		vertices,
		faces,
		materials: [
			{ color: 3, pattern: 0 },
			{ color: 5, pattern: 1 },
		],
	};
}

function selfTest(): void {
	const blob = cookModel3d(buildCubeSource());
	const chunks = parseUaf(blob);
	if (chunks.length !== 1 || chunks[0].type !== UafChunkType.Model3D) {
		throw new Error('self-test: chunk Model3D ausente');
	}
	const p = blob.subarray(chunks[0].offset, chunks[0].offset + chunks[0].size);
	const version = p.readUInt16BE(0);
	const nv = p.readUInt16BE(4);
	const nf = p.readUInt16BE(6);
	const ni = p.readUInt16BE(8);
	const nm = p.readUInt16BE(10);
	const vOff = p.readUInt32BE(12);
	const iOff = p.readUInt32BE(16);
	const fOff = p.readUInt32BE(20);
	const mOff = p.readUInt32BE(24);
	if (version !== MODEL3D_VERSION || nv !== 8 || nf !== 6 || ni !== 24 || nm !== 2) {
		throw new Error('self-test: conteos inesperados');
	}
	if (vOff < MODEL3D_HEADER_BYTES || iOff !== vOff + nv * 6 || fOff !== iOff + ni * 2 || mOff !== fOff + nf * 6) {
		throw new Error('self-test: offsets inesperados');
	}
	if (p.readInt16BE(vOff) !== -20 || p.readInt16BE(vOff + 6) !== 20) {
		throw new Error('self-test: vértices inesperados');
	}
	if (p.readUInt16BE(mOff) !== 0x0300 || p.readUInt16BE(mOff + 4) !== 0x0501) {
		throw new Error('self-test: materiales inesperados');
	}
	console.log(`OK cook-model3d: ${blob.length} bytes, ${nv} vértices, ${nf} caras, ${nm} materiales`);
}

function main(): void {
	const args = process.argv.slice(2);
	if (args.includes('--self-test')) {
		selfTest();
		return;
	}
	if (args.length < 2) {
		console.error('Uso: cook-model3d.js <model.json> <out.uafr> | --self-test');
		process.exit(2);
	}
	const src = JSON.parse(fs.readFileSync(args[0], 'utf8')) as ModelSource;
	const blob = cookModel3d(src);
	fs.writeFileSync(args[1], blob);
	console.log(`OK cook-model3d: ${blob.length} bytes -> ${args[1]}`);
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
	try {
		main();
	} catch (e) {
		console.error(String((e as Error).message ?? e));
		process.exit(1);
	}
}
