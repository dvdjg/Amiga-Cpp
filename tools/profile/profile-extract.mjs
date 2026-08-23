#!/usr/bin/env node
/**
 * profile-extract.mjs ??? Extrae los frames de un perfil de WinUAE-DBG (.bin).
 *
 * El perfil binario producido por `monitor profile`/canal lateral contiene, por
 * frame: registros custom, colores AGA, registros DMA, recursos graficos
 * (direcciones de bitplanes), ciclos y una captura de pantalla (jpg/png).
 *
 * Uso:
 *   node tools/profile/profile-extract.mjs <perfil.bin> <outDir>
 *
 * Genera en outDir:
 *   - frame_0000.jpg (o .png), frame_0001.jpg, ...   (captura de pantalla por frame)
 *   - profile-summary.json                            (metadatos: DMA, recursos, regs, ciclos)
 */
import * as fs from 'fs';
import * as path from 'path';

// Constantes del formato (coinciden con src/backend/profile.ts de vscode-amiga-debug).
const CUSTOM_REGS_LEN = 256 * 2 + 4 + 4; // 516
const AGA_COLORS_LEN = 256 * 4;          // 1024
const DMA_REC_LEGACY = 58;
const DMA_REC_EXTENDED = 121;
const RESOURCE_LEN = 52;
const NR_DMA_HPOS = 227;
const NR_DMA_VPOS = 313;
const DMA_COUNT = NR_DMA_HPOS * NR_DMA_VPOS;

function parseProfile(buffer) {
	const out = { sectionBases: [], systemStackLower: 0, systemStackUpper: 0, stackLower: 0, stackUpper: 0, kickRomSize: 0, chipMemSize: 0, bogoMemSize: 0, baseClock: 0, cpuCycleUnit: 0, frames: [] };
	let o = 0;
	const numFrames = buffer.readUInt32LE(o); o += 4;
	const sectionCount = buffer.readUInt32LE(o); o += 4;
	
	for (let i = 0; i < sectionCount; i++) { out.sectionBases.push(buffer.readUInt32LE(o)); o += 4; }
	out.systemStackLower = buffer.readUInt32LE(o); o += 4;
	out.systemStackUpper = buffer.readUInt32LE(o); o += 4;
	out.stackLower = buffer.readUInt32LE(o); o += 4;
	out.stackUpper = buffer.readUInt32LE(o); o += 4;
	out.kickRomSize = buffer.readUInt32LE(o); o += 4; o += out.kickRomSize;
	out.chipMemSize = buffer.readUInt32LE(o); o += 4; o += out.chipMemSize;
	out.bogoMemSize = buffer.readUInt32LE(o); o += 4; o += out.bogoMemSize;
	out.baseClock = buffer.readUInt32LE(o); o += 4;
	out.cpuCycleUnit = buffer.readUInt32LE(o); o += 4;
	

	for (let f = 0; f < numFrames; f++) {
		const frame = { chipsetFlags: 0, customRegs: [], agaColors: [], dmaSummary: { total: 0, byType: {} }, gfxResources: [], profileCycles: 0, idleCycles: 0, screenshot: null, screenshotType: 'jpg', screenshotSize: 0 };
		try {
		// custom registers
		const customRegsLen = buffer.readUInt32LE(o); o += 4;
		const customRegsStart = o; // el bloque incluye chipsetFlags (4) + 256 regs (512)
		frame.chipsetFlags = buffer.readUInt32BE(o); o += 4;
		frame.customRegs = new Array(256);
		for (let i = 0; i < 256; i++) { frame.customRegs[i] = buffer.readUInt16BE(o); o += 2; }
		o = customRegsStart + customRegsLen;

		// AGA colors
		const agaColorsLen = buffer.readUInt32LE(o); o += 4;
		if (agaColorsLen === AGA_COLORS_LEN) {
			frame.agaColors = new Array(256);
			for (let i = 0; i < 256; i++) { frame.agaColors[i] = buffer.readUInt32BE(o); o += 4; }
		}

		// DMA
		const dmaLen = buffer.readUInt32LE(o); o += 4;
		const dmaCount = buffer.readUInt32LE(o); o += 4;
		
		const isExtended = dmaLen === DMA_REC_EXTENDED;
		const dmaOffsets = isExtended ? { type: 77 } : { type: 29 };
		const dmaBuf = Buffer.from(buffer.buffer, bufferOffsetOf(buffer, o), dmaLen * dmaCount);
		o += dmaLen * dmaCount;
		for (let i = 0; i < dmaCount; i++) {
			const type = dmaBuf.readInt16LE(i * dmaLen + dmaOffsets.type);
			if (type !== 0 && type !== undefined) {
				frame.dmaSummary.total++;
				frame.dmaSummary.byType[type] = (frame.dmaSummary.byType[type] || 0) + 1;
			}
		}

		// resources (bitplanes, paleta)
		const resourceLen = buffer.readUInt32LE(o); o += 4;
		const resourceCount = buffer.readUInt32LE(o); o += 4;
		const resBuf = Buffer.from(buffer.buffer, bufferOffsetOf(buffer, o), resourceLen * resourceCount);
		o += resourceLen * resourceCount;
		for (let i = 0; i < resourceCount; i++) {
			const base = i * resourceLen;
			const address = resBuf.readUInt32LE(base + 0);
			const size = resBuf.readUInt32LE(base + 4);
			let nameEnd = resBuf.indexOf(0, base + 8);
			if (nameEnd === -1) nameEnd = base + 40;
			const name = resBuf.toString('utf8', base + 8, nameEnd);
			const type = resBuf.readUInt16LE(base + 40);
			const flags = resBuf.readUInt16LE(base + 42);
			const res = { address, size, name, type, flags };
			if (type === 0) { // bitmap
				res.width = resBuf.readUInt16LE(base + 44);
				res.height = resBuf.readUInt16LE(base + 46);
				res.numPlanes = resBuf.readUInt16LE(base + 48);
			} else if (type === 1) { // palette
				res.numEntries = resBuf.readUInt16LE(base + 44);
			}
			frame.gfxResources.push(res);
		}

		frame.profileCycles = buffer.readUInt32LE(o); o += 4;
		frame.idleCycles = buffer.readUInt32LE(o); o += 4;

		// profile array (call stack indices)
		const profileCount = buffer.readUInt32LE(o); o += 4;
		o += profileCount * 4;

		// screenshot
		const screenshotSize = buffer.readUInt32LE(o); o += 4;
		const screenshotType = buffer.readUInt32LE(o); o += 4;
		frame.screenshotType = screenshotType === 0 ? 'jpg' : 'png';
		frame.screenshotSize = screenshotSize;
		frame.screenshot = Buffer.from(buffer.buffer, bufferOffsetOf(buffer, o), screenshotSize);
		o += screenshotSize;

		out.frames.push(frame);
		} catch (e) {
			throw new Error(`Fallo en frame ${f} (offset 0x${o.toString(16)}): ${e.message}`);
		}
	}
	return out;
}

// Node's Buffer.from(buffer.buffer, offset, len) espera un offset ABSOLUTO del ArrayBuffer.
function bufferOffsetOf(buffer, offset) {
	return buffer.byteOffset + offset;
}

function main() {
	const [binPath, outDir] = process.argv.slice(2);
	if (!binPath || !outDir) {
		console.error('Uso: node tools/profile/profile-extract.mjs <perfil.bin> <outDir>');
		process.exit(2);
	}
	const buffer = fs.readFileSync(binPath);
	const profile = parseProfile(buffer);
	fs.mkdirSync(outDir, { recursive: true });

	const summary = {
		file: path.basename(binPath),
		sectionBases: profile.sectionBases.map((v) => '0x' + v.toString(16)),
		baseClock: profile.baseClock,
		cpuCycleUnit: profile.cpuCycleUnit,
		numFrames: profile.frames.length,
		frames: []
	};

	profile.frames.forEach((frame, i) => {
		const ext = frame.screenshotType;
		const frameFile = path.join(outDir, `frame_${String(i).padStart(4, '0')}.${ext}`);
		fs.writeFileSync(frameFile, frame.screenshot);

		const regs = {};
		for (const [name, idx] of Object.entries({
			BPLCON0: 0x100, BPLCON1: 0x102, BPLCON2: 0x104, BPL1MOD: 0x108, BPL2MOD: 0x10a,
			DIWSTRT: 0x08e, DIWSTOP: 0x090, DDFSTRT: 0x092, DDFSTOP: 0x094,
			BPL0PT: 0x0e0, BPL1PT: 0x0e2, BPL2PT: 0x0e4, BPL3PT: 0x0e6, BPL4PT: 0x0e8, BPL5PT: 0x0ea,
			COLOR00: 0x180, COLOR01: 0x182
		})) {
			const customIdx = idx / 2;
			if (customIdx >= 0 && customIdx < 256) regs[name] = frame.customRegs[customIdx];
		}

		summary.frames.push({
			frame: i,
			screenshot: path.basename(frameFile),
			chipsetFlags: frame.chipsetFlags,
			registers: regs,
			bitplanes: frame.gfxResources.filter((r) => r.type === 0),
			palette: frame.gfxResources.filter((r) => r.type === 1),
			dma: frame.dmaSummary,
			profileCycles: frame.profileCycles,
			idleCycles: frame.idleCycles
		});
	});

	fs.writeFileSync(path.join(outDir, 'profile-summary.json'), JSON.stringify(summary, null, 2));
	console.log(`OK ${profile.frames.length} frame(s) extraidos a ${outDir}`);
}

main();

