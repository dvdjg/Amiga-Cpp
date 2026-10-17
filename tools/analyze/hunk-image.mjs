// ---------------------------------------------------------------------------
// Extrae la imagen de memoria (base 0) de un ejecutable Amiga en formato HUNK,
// APLICANDO las relocaciones (vlink deja HUNK_RELOC32 para que el loader las
// aplique; con base 0 son sumas directas). Salida: binario plano, listo para
// `m68k-amiga-elf-objdump -D -b binary -m m68k:68000 --adjust-vma=<dir>`.
//
// Para qué: usar el binario del original como ORÁCULO de codegen — disassemblar
// sus funciones (direcciones del `.map`) y comparar instrucción a instrucción
// con nuestro build. Metodología y caso de uso: campaña 116 (DT-026) y
// docs/guides/optimization/OPTIMIZACION_GPP_68000.md (entradas 2026-10-17).
//
// Uso: node tools/analyze/hunk-image.mjs <entrada.exe> <salida.bin>
// ---------------------------------------------------------------------------
import { readFileSync, writeFileSync } from 'node:fs';

const [inPath, outPath] = process.argv.slice(2);
if (!inPath || !outPath) {
	console.error('uso: node tools/analyze/hunk-image.mjs <entrada.exe> <salida.bin>');
	process.exit(2);
}
const buf = readFileSync(inPath);
let pos = 0;
const be32 = (o) => buf.readUInt32BE(o);
const hunks = [];

while (pos + 4 <= buf.length) {
	const type = be32(pos);
	pos += 4;
	const base = type & 0x3fffffff;
	if (base === 0x3f3) { // HUNK_HEADER: nombres de residentes + tabla de tamanos
		pos += 4;
		const tableSize = be32(pos); pos += 4;
		pos += 8;
		for (let i = 0; i < tableSize; i++) pos += 4;
		continue;
	}
	if (base === 0x3e9 || base === 0x3ea) { // HUNK_CODE / HUNK_DATA
		const words = be32(pos); pos += 4;
		const bytes = words * 4;
		hunks.push({ kind: base === 0x3e9 ? 'CODE' : 'DATA', data: Buffer.from(buf.subarray(pos, pos + bytes)) });
		pos += bytes;
		continue;
	}
	if (base === 0x3eb) { // HUNK_BSS
		const words = be32(pos); pos += 4;
		hunks.push({ kind: 'BSS', data: Buffer.alloc(words * 4) });
		continue;
	}
	if (base === 0x3ec) { // HUNK_RELOC32: listas (count, hunk) acabadas en count=0
		const relocLists = [];
		while (true) {
			const count = be32(pos); pos += 4;
			if (count === 0) break;
			const target = be32(pos); pos += 4;
			const offsets = [];
			for (let i = 0; i < count; i++) { offsets.push(be32(pos)); pos += 4; }
			relocLists.push({ target, offsets });
		}
		hunks.push({ kind: 'RELOC32', relocLists });
		continue;
	}
	if (base === 0x3e8) { const n = be32(pos); pos += 4; pos += n * 4; continue; } // NAME
	if (base === 0x3e7) { continue; } // UNIT (.o)
	if (base === 0x3f2) { hunks.push({ kind: 'END' }); continue; }
	if (base === 0x3f1) { let n; do { n = be32(pos); pos += 4; pos += 4; pos += n * 4; } while (n !== 0); continue; } // SYMBOL
	if (base === 0x3f6) { const n = be32(pos); pos += 4; pos += n * 4; continue; } // DEBUG
	console.error(`hunk desconocido 0x${type.toString(16)} @${pos - 4}`);
	break;
}

const memHunks = hunks.filter((h) => h.kind !== 'RELOC32' && h.kind !== 'END');
const bases = [];
let addr = 0;
for (const h of memHunks) { bases.push(addr); addr += h.data.length; }
const image = Buffer.concat(memHunks.map((h) => h.data));

let memIdx = -1, relocCount = 0;
for (const h of hunks) {
	if (h.kind === 'RELOC32') {
		for (const { target, offsets } of h.relocLists) {
			const add = bases[target];
			for (const off of offsets) {
				if (off + 4 > memHunks[memIdx].data.length) { console.error(`reloc fuera de rango hunk ${memIdx} @${off}`); continue; }
				const cur = image.readUInt32BE(bases[memIdx] + off);
				image.writeUInt32BE((cur + add) >>> 0, bases[memIdx] + off);
				relocCount++;
			}
		}
		continue;
	}
	if (h.kind === 'END') continue;
	memIdx++;
}

writeFileSync(outPath, image);
console.log(`hunks memoria: ${memHunks.map((h, i) => `${h.kind}@0x${bases[i].toString(16)}+0x${h.data.length.toString(16)}`).join(', ')}`);
console.log(`relocaciones aplicadas: ${relocCount}`);
console.log(`imagen: ${image.length} bytes -> ${outPath}`);
