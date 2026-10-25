#!/usr/bin/env node
// Resuelve un símbolo del ELF a su dirección runtime usando las bases de sección
// publicadas por el canal lateral. No deriva .data/.bss de baseText: alinea la
// lista de secciones SHF_ALLOC del ELF con state.sections.
//
// Uso:
//   node tools/debug/runtime-symbols.mjs <demo|leaf> <símbolo> [--config A500_debug] [--json]
//   node tools/debug/runtime-symbols.mjs --report out/run/<leaf>/<cfg>/run-report.json <símbolo>
//
// El reporte debe incluir sideChannel.state.sections (run-demo lo guarda al conectar).
// Un símbolo puede darse completo o por sufijo único (p. ej. g_guarded).

import fs from 'node:fs';
import path from 'node:path';
import process from 'node:process';
import { fileURLToPath } from 'node:url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const SHF_ALLOC = 0x2;
const SHT_SYMTAB = 2;

function usage() {
	console.error('Uso: node tools/debug/runtime-symbols.mjs <demo|leaf> <símbolo> [--config A500_debug] [--json]');
	console.error('     node tools/debug/runtime-symbols.mjs --report <run-report.json> <símbolo> [--json]');
	process.exit(2);
}

function parseArgs(argv) {
	const out = { config: 'A500_debug', json: false, report: '', elf: '', positional: [] };
	for (let i = 0; i < argv.length; ++i) {
		const a = argv[i];
		if (a === '--json') out.json = true;
		else if (a === '--config') out.config = argv[++i] ?? '';
		else if (a === '--report') out.report = argv[++i] ?? '';
		else if (a === '--elf') out.elf = argv[++i] ?? '';
		else if (a === '--help' || a === '-h') usage();
		else if (a.startsWith('-')) {
			console.error(`Argumento desconocido: ${a}`);
			usage();
		} else out.positional.push(a);
	}
	return out;
}

function readElfSections(file) {
	const b = fs.readFileSync(file);
	if (b.length < 0x34 || b.readUInt32BE(0) !== 0x7f454c46) {
		throw new Error(`No es un ELF válido: ${file}`);
	}
	if (b[4] !== 1) throw new Error('Solo se soporta ELF32');
	if (b[5] !== 2) throw new Error('Solo se soporta ELF big-endian (m68k)');
	const shoff = b.readUInt32BE(0x20);
	const shentsize = b.readUInt16BE(0x2e);
	const shnum = b.readUInt16BE(0x30);
	const shstrndx = b.readUInt16BE(0x32);
	if (shentsize !== 40 || shoff === 0 || shnum === 0 || shstrndx >= shnum) {
		throw new Error('Tabla de secciones ELF no soportada');
	}
	const sectionAt = (i) => {
		const o = shoff + i * shentsize;
		return {
			nameOff: b.readUInt32BE(o),
			type: b.readUInt32BE(o + 4),
			flags: b.readUInt32BE(o + 8),
			addr: b.readUInt32BE(o + 12),
			offset: b.readUInt32BE(o + 16),
			size: b.readUInt32BE(o + 20),
			link: b.readUInt32BE(o + 24),
			shndx: i,
		};
	};
	const raw = Array.from({ length: shnum }, (_, i) => sectionAt(i));
	const shstr = raw[shstrndx];
	const names = b.subarray(shstr.offset, shstr.offset + shstr.size);
	const nameOf = (sec) => {
		const end = names.indexOf(0, sec.nameOff);
		return names.subarray(sec.nameOff, end < 0 ? names.length : end).toString('utf8');
	};
	const sections = raw.map((s) => ({ ...s, name: nameOf(s) }));
	return { buffer: b, sections };
}

function elfSymbols(file, sections) {
	const { buffer } = readElfSections(file);
	const symtab = sections.find((s) => s.type === SHT_SYMTAB);
	if (!symtab) throw new Error('El ELF no tiene .symtab');
	const str = sections[symtab.link];
	if (!str) throw new Error('El .symtab no tiene string table');
	const strings = buffer.subarray(str.offset, str.offset + str.size);
	const symbols = [];
	for (let off = symtab.offset; off + 16 <= symtab.offset + symtab.size; off += 16) {
		const nameOff = buffer.readUInt32BE(off);
		const value = buffer.readUInt32BE(off + 4);
		const size = buffer.readUInt32BE(off + 8);
		const info = buffer[off + 12];
		const shndx = buffer.readUInt16BE(off + 14);
		const end = strings.indexOf(0, nameOff);
		const name = strings.subarray(nameOff, end < 0 ? strings.length : end).toString('utf8');
		if (name) symbols.push({ name, value, size, type: info & 0x0f, shndx });
	}
	return symbols;
}

function runtimeSections(report) {
	const candidates = [
		report?.sideChannel?.state?.sections,
		report?.finalSideChannel?.sections,
		report?.sideChannel?.sections,
	];
	const list = candidates.find((v) => Array.isArray(v) && v.length > 0);
	if (!list) throw new Error('El run-report no contiene sideChannel.state.sections');
	return list.map((v) => {
		const n = typeof v === 'number' ? v : Number.parseInt(String(v), 16);
		if (!Number.isFinite(n)) throw new Error(`Base de sección inválida: ${v}`);
		return n >>> 0;
	});
}

function reportPathFor(demo, config) {
	const leaf = path.basename(String(demo).replace(/\\/g, '/'));
	return path.join(ROOT, 'out/run', leaf, config, 'run-report.json');
}

function main() {
	const args = parseArgs(process.argv.slice(2));
	if (!args.report && args.positional.length < 2) usage();
	const symbolQuery = args.report ? args.positional[0] : args.positional[1];
	const demo = args.report ? '' : args.positional[0];
	if (!symbolQuery) usage();

	const reportPath = args.report
		? path.resolve(args.report)
		: reportPathFor(demo, args.config);
	if (!fs.existsSync(reportPath)) throw new Error(`No existe el reporte: ${reportPath}`);
	const report = JSON.parse(fs.readFileSync(reportPath, 'utf8'));
	const elfPath = args.elf
		? path.resolve(args.elf)
		: (report.map ?? report.executable ?? '').replace(/\.map$/, '.elf');
	if (!elfPath || !fs.existsSync(elfPath)) throw new Error(`No existe el ELF: ${elfPath}`);

	const { sections } = readElfSections(elfPath);
	const alloc = sections.filter((s) => (s.flags & SHF_ALLOC) !== 0 && s.size !== 0);
	const bases = runtimeSections(report);
	if (alloc.length !== bases.length) {
		const names = alloc.map((s) => `${s.name}@0x${s.addr.toString(16)}+0x${s.size.toString(16)}`);
		throw new Error(
			`Desalineadas ${alloc.length} secciones SHF_ALLOC del ELF y ${bases.length} bases runtime.\n` +
			`ELF: ${names.join(', ')}\nRuntime: ${bases.map((v) => `0x${v.toString(16)}`).join(', ')}`,
		);
	}

	const symbols = elfSymbols(elfPath, sections);
	let matches = symbols.filter((s) => s.name === symbolQuery && s.shndx !== 0);
	if (matches.length === 0) {
		matches = symbols.filter((s) => s.shndx !== 0 && s.name.includes(symbolQuery));
	}
	if (matches.length === 0) throw new Error(`Símbolo no encontrado: ${symbolQuery}`);
	if (matches.length > 1) {
		throw new Error(`Símbolo ambiguo: ${symbolQuery}\n` +
			matches.map((s) => `  ${s.name}`).join('\n'));
	}
	const sym = matches[0];
	const allocIndex = alloc.findIndex((s) => s.shndx === sym.shndx);
	if (allocIndex < 0) throw new Error(`El símbolo ${sym.name} no está en una sección SHF_ALLOC`);
	const sec = alloc[allocIndex];
	const runtime = (bases[allocIndex] + (sym.value - sec.addr)) >>> 0;
	const base = bases[allocIndex];
	const result = {
		symbol: sym.name,
		elfAddress: sym.value,
		section: sec.name,
		sectionElfAddress: sec.addr,
		sectionRuntimeBase: base,
		runtimeAddress: runtime,
		report: reportPath,
		elf: elfPath,
	};
	if (args.json) {
		console.log(JSON.stringify(result, null, 2));
	} else {
		console.log(`${sym.name} -> 0x${runtime.toString(16).padStart(8, '0')} ` +
			`(${sec.name}: base 0x${base.toString(16).padStart(8, '0')} + ` +
			`0x${(sym.value - sec.addr).toString(16)})`);
	}
}

try {
	main();
} catch (error) {
	console.error(`runtime-symbols: ${error.message}`);
	process.exit(1);
}
