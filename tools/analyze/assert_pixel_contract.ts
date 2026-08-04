#!/usr/bin/env node
/**
 * Motor de aserciones de pixeles: valida una secuencia de frames contra un
 * pixel-contract.json. Sustituye a assert-pixel-contract.py (Pillow) por una
 * version Node/pngjs con la misma logica.
 *
 * Soporta checks globales (`forbidden_color_ratio`) y por segmento
 * (`equal_region`, `shifted_region_match`, `telemetry_shift_match`,
 * `telemetry_direction_match`), con overlays de fallo en out/overlays.
 *
 * Uso: node dist/tools/analyze/assert_pixel_contract.js --sequence-dir <dir>
 *      --contract <pixel-contract.json> [--run-report <run-report.json>]
 *      [--out-dir <dir>]
 */
import * as fs from 'fs';
import * as path from 'path';
import { argValue, fail } from '../lib/cli.js';
import { readPng, createImage, savePng, pixel, setPixel, RgbaImage } from '../lib/image.js';

interface Viewport {
	x: number;
	y: number;
	w: number;
	h: number;
}

interface CoordinateSpace {
	logicalW: number;
	logicalH: number;
}

function readJson(filePath: string): any {
	return JSON.parse(fs.readFileSync(filePath, 'utf-8'));
}

function listFrames(sequenceDir: string): string[] {
	const frames = fs.readdirSync(sequenceDir)
		.filter((name) => name.startsWith('frame_') && name.endsWith('.png'))
		.sort()
		.map((name) => path.join(sequenceDir, name));
	if (frames.length === 0) {
		fail(`No se encontraron frame_*.png en ${sequenceDir}`);
	}
	return frames;
}

/** Localiza el rectangulo de contenido (no negro) del primer frame. */
function detectNonBlackViewport(image: RgbaImage, threshold = 8): Viewport {
	const { width, height, data } = image;
	let left = width;
	let right = -1;
	let top = height;
	let bottom = -1;
	for (let y = 0; y < height; ++y) {
		for (let x = 0; x < width; ++x) {
			const i = (y * width + x) * 4;
			const max = Math.max(data[i], data[i + 1], data[i + 2]);
			if (max > threshold) {
				if (x < left) left = x;
				if (x > right) right = x;
				if (y < top) top = y;
				if (y > bottom) bottom = y;
			}
		}
	}
	if (right < left || bottom < top) {
		return { x: 0, y: 0, w: width, h: height };
	}
	return { x: left, y: top, w: right - left + 1, h: bottom - top + 1 };
}

function resolveViewport(contract: any, firstFrame: string): Viewport {
	const viewportCfg = contract.viewport ?? { mode: 'auto_non_black' };
	const mode = viewportCfg.mode ?? 'auto_non_black';
	const image = readPng(firstFrame);
	if (mode === 'auto_non_black') {
		const threshold = parseInt(viewportCfg.threshold ?? 8, 10);
		return detectNonBlackViewport(image, threshold);
	}
	if (mode === 'fixed') {
		return {
			x: parseInt(viewportCfg.x ?? 0, 10),
			y: parseInt(viewportCfg.y ?? 0, 10),
			w: parseInt(viewportCfg.w ?? image.width, 10),
			h: parseInt(viewportCfg.h ?? image.height, 10),
		};
	}
	fail(`viewport.mode no soportado: ${mode}`);
}

function resolveCoordinateSpace(contract: any, viewport: Viewport): CoordinateSpace {
	const viewportCfg = contract.viewport ?? {};
	let logicalW = parseInt(viewportCfg.logicalWidth ?? viewport.w, 10);
	let logicalH = parseInt(viewportCfg.logicalHeight ?? viewport.h, 10);
	logicalW = Math.max(1, logicalW);
	logicalH = Math.max(1, logicalH);
	return { logicalW, logicalH };
}

function clampRoi(roi: any, viewport: Viewport, frameW: number, frameH: number, coords: CoordinateSpace): [number, number, number, number] {
	const logicalX = parseInt(roi.x ?? 0, 10);
	const logicalY = parseInt(roi.y ?? 0, 10);
	const logicalW = parseInt(roi.w ?? coords.logicalW, 10);
	const logicalH = parseInt(roi.h ?? coords.logicalH, 10);

	const scaleX = viewport.w / coords.logicalW;
	const scaleY = viewport.h / coords.logicalH;

	const x = viewport.x + Math.round(logicalX * scaleX);
	const y = viewport.y + Math.round(logicalY * scaleY);
	const w = Math.max(1, Math.round(logicalW * scaleX));
	const h = Math.max(1, Math.round(logicalH * scaleY));

	const x0 = Math.max(0, x);
	const y0 = Math.max(0, y);
	const x1 = Math.min(frameW, x + w);
	const y1 = Math.min(frameH, y + h);
	if (x1 <= x0 || y1 <= y0) {
		return [0, 0, 0, 0];
	}
	return [x0, y0, x1 - x0, y1 - y0];
}

function pixelDiffExceeds(a: [number, number, number], b: [number, number, number], tolerance: number): boolean {
	return (
		Math.abs(a[0] - b[0]) > tolerance ||
		Math.abs(a[1] - b[1]) > tolerance ||
		Math.abs(a[2] - b[2]) > tolerance
	);
}

function directionName(dx: number, dy: number): string {
	if (dx === 0 && dy === 0) return 'static';
	const horizontal = dx > 0 ? 'right' : dx < 0 ? 'left' : '';
	const vertical = dy > 0 ? 'down' : dy < 0 ? 'up' : '';
	if (horizontal && vertical) return `${vertical}-${horizontal}`;
	return horizontal || vertical;
}

function directionCompatible(observed: string, expected: string): boolean {
	if (expected === 'static') return observed === 'static';
	const parts = expected.split('-');
	return parts.every((part) => observed.includes(part));
}

function shiftedRegionErrorRatio(
	a: RgbaImage,
	b: RgbaImage,
	roiAbs: [number, number, number, number],
	dx: number,
	dy: number,
	tolerance: number,
): { compared: number; mismatched: number; errorRatio: number } {
	const [x0, y0, w, h] = roiAbs;
	if (w <= 0 || h <= 0) return { compared: 0, mismatched: 0, errorRatio: 1.0 };
	let compared = 0;
	let mismatched = 0;
	for (let y = y0; y < y0 + h; ++y) {
		const by = y + dy;
		if (by < 0 || by >= b.height) continue;
		for (let x = x0; x < x0 + w; ++x) {
			const bx = x + dx;
			if (bx < 0 || bx >= b.width) continue;
			compared++;
			if (pixelDiffExceeds(pixel(a, x, y), pixel(b, bx, by), tolerance)) mismatched++;
		}
	}
	if (compared === 0) return { compared: 0, mismatched: 0, errorRatio: 1.0 };
	return { compared, mismatched, errorRatio: mismatched / compared };
}

/** Escribe un overlay RGBA marcando en rojo los pixeles que no coinciden. */
function writeMismatchOverlay(
	a: RgbaImage,
	b: RgbaImage,
	roiAbs: [number, number, number, number],
	dx: number,
	dy: number,
	tolerance: number,
	outPath: string,
): number {
	const [x0, y0, w, h] = roiAbs;
	if (w <= 0 || h <= 0) return 0;
	const overlay = createImage(b.width, b.height);
	overlay.data.set(b.data);
	let mismatches = 0;
	for (let y = y0; y < y0 + h; ++y) {
		const by = y + dy;
		if (by < 0 || by >= b.height) continue;
		for (let x = x0; x < x0 + w; ++x) {
			const bx = x + dx;
			if (bx < 0 || bx >= b.width) continue;
			if (pixelDiffExceeds(pixel(a, x, y), pixel(b, bx, by), tolerance)) {
				mismatches++;
				const [, g, blue] = pixel(b, bx, by);
				setPixel(overlay, bx, by, 255, Math.max(0, g - 80), Math.max(0, blue - 80), 255);
			}
		}
	}
	savePng(overlay, outPath);
	return mismatches;
}

function bestShiftForRoi(
	a: RgbaImage,
	b: RgbaImage,
	roiAbs: [number, number, number, number],
	tolerance: number,
	searchRadius: number,
): { dx: number; dy: number; errorRatio: number; compared: number; mismatched: number; direction: string } {
	let best = { dx: 0, dy: 0, errorRatio: 1.0, compared: 0, mismatched: 0 };
	for (let dy = -searchRadius; dy <= searchRadius; ++dy) {
		for (let dx = -searchRadius; dx <= searchRadius; ++dx) {
			const result = shiftedRegionErrorRatio(a, b, roiAbs, dx, dy, tolerance);
			if (result.errorRatio < best.errorRatio) {
				best = { dx, dy, ...result };
			}
		}
	}
	return { ...best, direction: directionName(best.dx, best.dy) };
}

function forbiddenColorRatio(
	image: RgbaImage,
	roiAbs: [number, number, number, number],
	color: [number, number, number],
	tolerance: number,
): { total: number; matches: number; ratio: number } {
	const [x0, y0, w, h] = roiAbs;
	if (w <= 0 || h <= 0) return { total: 0, matches: 0, ratio: 1.0 };
	let total = 0;
	let matches = 0;
	for (let y = y0; y < y0 + h; ++y) {
		for (let x = x0; x < x0 + w; ++x) {
			total++;
			const p = pixel(image, x, y);
			if (
				Math.abs(p[0] - color[0]) <= tolerance &&
				Math.abs(p[1] - color[1]) <= tolerance &&
				Math.abs(p[2] - color[2]) <= tolerance
			) {
				matches++;
			}
		}
	}
	if (total === 0) return { total: 0, matches: 0, ratio: 1.0 };
	return { total, matches, ratio: matches / total };
}

function writeForbiddenOverlay(
	image: RgbaImage,
	roiAbs: [number, number, number, number],
	color: [number, number, number],
	tolerance: number,
	outPath: string,
): number {
	const [x0, y0, w, h] = roiAbs;
	if (w <= 0 || h <= 0) return 0;
	const overlay = createImage(image.width, image.height);
	overlay.data.set(image.data);
	let marked = 0;
	for (let y = y0; y < y0 + h; ++y) {
		for (let x = x0; x < x0 + w; ++x) {
			const p = pixel(image, x, y);
			if (
				Math.abs(p[0] - color[0]) <= tolerance &&
				Math.abs(p[1] - color[1]) <= tolerance &&
				Math.abs(p[2] - color[2]) <= tolerance
			) {
				marked++;
				const [, g, blue] = p;
				setPixel(overlay, x, y, 255, Math.max(0, g - 80), Math.max(0, blue - 80), 255);
			}
		}
	}
	savePng(overlay, outPath);
	return marked;
}

function pairRange(segment: any, frameCount: number): [number, number] {
	const frames = segment.frames;
	if (!Array.isArray(frames) || frames.length !== 2) {
		return [0, Math.max(0, frameCount - 1)];
	}
	let start = parseInt(frames[0], 10);
	let end = parseInt(frames[1], 10);
	if (end === -1) end = frameCount - 1;
	start = Math.max(0, Math.min(start, frameCount - 1));
	end = Math.max(0, Math.min(end, frameCount - 1));
	if (end < start) [start, end] = [end, start];
	return [start, end];
}

function loadTelemetry(runReportPath: string): any[] {
	const report = readJson(runReportPath);
	const frames = (report.sequence ?? {}).frames ?? [];
	const telemetry: any[] = [];
	for (const frame of frames) {
		const rs = frame.runStatus ?? frame.runStatusBefore;
		if (!rs || !rs.ok) continue;
		const detail = parseInt(rs.detail ?? 0, 10);
		telemetry.push({
			frame: parseInt(rs.frame ?? 0, 10),
			cameraX: (detail >> 16) & 0xff,
			cameraY: (detail >> 8) & 0xff,
			tileJobs: (detail >> 4) & 0x0f,
			prefetchFlags: detail & 0x0f,
		});
	}
	return telemetry;
}

function runAssertions(
	frames: string[],
	contract: any,
	viewport: Viewport,
	coords: CoordinateSpace,
	runReportPath: string | null,
	overlaysDir: string,
): any {
	const defaults = contract.defaults ?? {};
	const defaultTolerance = parseInt(defaults.rgbTolerance ?? 8, 10);
	const defaultErrorRatio = parseFloat(defaults.maxErrorRatio ?? 0.01);

	const frameImages = frames.map(readPng);
	const frameSize = { width: frameImages[0].width, height: frameImages[0].height };
	let telemetry: any[] = [];
	if (runReportPath && fs.existsSync(runReportPath)) {
		telemetry = loadTelemetry(runReportPath);
	}

	const checksReport: any[] = [];
	const failures: any[] = [];

	for (const check of contract.globalChecks ?? []) {
		const ctype = check.type ?? '';
		if (ctype !== 'forbidden_color_ratio') continue;
		const color: [number, number, number] = [
			parseInt(check.color?.[0] ?? 0, 10),
			parseInt(check.color?.[1] ?? 0, 10),
			parseInt(check.color?.[2] ?? 0, 10),
		];
		const tolerance = parseInt(check.colorTolerance ?? 0, 10);
		const maxRatio = parseFloat(check.maxRatio ?? 0.0);
		const ignoreFirstFrames = Math.max(0, parseInt(check.ignoreFirstFrames ?? 0, 10));
		const roi = check.roi ?? { x: 0, y: 0, w: viewport.w, h: viewport.h };
		const roiAbs = clampRoi(roi, viewport, frameSize.width, frameSize.height, coords);

		const perFrame: any[] = [];
		for (let index = 0; index < frames.length; ++index) {
			const result = forbiddenColorRatio(frameImages[index], roiAbs, color, tolerance);
			perFrame.push({ frame: index, ...result });
		}

		let consideredFrames = perFrame.filter((item) => item.frame >= ignoreFirstFrames);
		if (consideredFrames.length === 0) consideredFrames = perFrame;

		let maxSeen = 0.0;
		let maxSeenFrame = consideredFrames.length > 0 ? consideredFrames[0].frame : 0;
		for (const frameItem of consideredFrames) {
			if (frameItem.ratio > maxSeen) {
				maxSeen = frameItem.ratio;
				maxSeenFrame = frameItem.frame;
			}
		}

		const passed = maxSeen <= maxRatio;
		const item = {
			scope: 'global',
			type: ctype,
			name: check.name ?? ctype,
			roi: { x: roiAbs[0], y: roiAbs[1], w: roiAbs[2], h: roiAbs[3] },
			maxRatio,
			ignoreFirstFrames,
			maxSeenRatio: maxSeen,
			maxSeenFrame,
			passed,
			details: perFrame,
			consideredFrameCount: consideredFrames.length,
		};
		checksReport.push(item);
		if (!passed) {
			const failedFrames = consideredFrames.filter((frameItem) => frameItem.ratio > maxRatio).map((f) => f.frame);
			for (const frameIndex of failedFrames) {
				const overlayName = `global_${ctype}_f${String(frameIndex).padStart(3, '0')}.png`;
				const overlayPath = path.join(overlaysDir, overlayName);
				writeForbiddenOverlay(frameImages[frameIndex], roiAbs, color, tolerance, overlayPath);
				const found = perFrame.find((frameItem) => frameItem.frame === frameIndex);
				if (found) found.overlay = overlayPath;
			}
			failures.push({
				name: item.name,
				reason: `maxSeenRatio=${maxSeen.toFixed(6)} > maxRatio=${maxRatio.toFixed(6)}`,
				frame: maxSeenFrame,
			});
		}
	}

	for (const segment of contract.segments ?? []) {
		const [start, end] = pairRange(segment, frames.length);
		const segmentName = segment.name ?? 'segment';
		for (const check of segment.checks ?? []) {
			const ctype = check.type ?? '';
			const roi = check.roi ?? { x: 0, y: 0, w: viewport.w, h: viewport.h };
			const roiAbs = clampRoi(roi, viewport, frameSize.width, frameSize.height, coords);
			const tolerance = parseInt(check.rgbTolerance ?? defaultTolerance, 10);
			const maxErrorRatio = parseFloat(check.maxErrorRatio ?? defaultErrorRatio);
			const ignoreFirstPairs = Math.max(0, parseInt(check.ignoreFirstPairs ?? 0, 10));

			const pairResults: any[] = [];
			let worstRatio = 0.0;
			let worstPair = [start, Math.min(start + 1, frames.length - 1)];

			for (let i = start; i < end; ++i) {
				if (i + 1 >= frames.length) break;
				let dx = 0;
				let dy = 0;
				if (ctype === 'shifted_region_match') {
					const logicalDx = parseInt(check.dx ?? 0, 10);
					const logicalDy = parseInt(check.dy ?? 0, 10);
					dx = Math.round(logicalDx * (viewport.w / coords.logicalW));
					dy = Math.round(logicalDy * (viewport.h / coords.logicalH));
				} else if (ctype === 'equal_region') {
					dx = 0;
					dy = 0;
				} else if (ctype === 'telemetry_shift_match') {
					const factorX = parseInt(check.cameraXToContentDx ?? 0, 10);
					const factorY = parseInt(check.cameraYToContentDy ?? 0, 10);
					if (i + 1 < telemetry.length) {
						const camDx = telemetry[i + 1].cameraX - telemetry[i].cameraX;
						const camDy = telemetry[i + 1].cameraY - telemetry[i].cameraY;
						dx = Math.round(camDx * factorX * (viewport.w / coords.logicalW));
						dy = Math.round(camDy * factorY * (viewport.h / coords.logicalH));
					}
				} else if (ctype === 'telemetry_direction_match') {
					const factorX = parseInt(check.cameraXToContentDx ?? 0, 10);
					const factorY = parseInt(check.cameraYToContentDy ?? 0, 10);
					let expected = 'static';
					if (i + 1 < telemetry.length) {
						const camDx = telemetry[i + 1].cameraX - telemetry[i].cameraX;
						const camDy = telemetry[i + 1].cameraY - telemetry[i].cameraY;
						dx = Math.round(camDx * factorX * (viewport.w / coords.logicalW));
						dy = Math.round(camDy * factorY * (viewport.h / coords.logicalH));
						expected = directionName(dx, dy);
					}
					const searchRadius = parseInt(check.searchRadius ?? 8, 10);
					const best = bestShiftForRoi(frameImages[i], frameImages[i + 1], roiAbs, tolerance, searchRadius);
					const compatible = directionCompatible(best.direction, expected);
					const pairItem = {
						from: i,
						to: i + 1,
						expectedDirection: expected,
						observedDirection: best.direction,
						observedDx: best.dx,
						observedDy: best.dy,
						errorRatio: best.errorRatio,
						compared: best.compared,
						mismatched: best.mismatched,
						compatible,
					};
					pairResults.push(pairItem);
					if (best.errorRatio > worstRatio) {
						worstRatio = best.errorRatio;
						worstPair = [i, i + 1];
					}
					continue;
				} else {
					continue;
				}

				const result = shiftedRegionErrorRatio(frameImages[i], frameImages[i + 1], roiAbs, dx, dy, tolerance);
				const errorRatio = result.errorRatio;
				pairResults.push({
					from: i,
					to: i + 1,
					dx,
					dy,
					errorRatio,
					compared: result.compared,
					mismatched: result.mismatched,
				});
				if (errorRatio > worstRatio) {
					worstRatio = errorRatio;
					worstPair = [i, i + 1];
				}
			}

			let consideredPairs = pairResults.filter((item) => item.from >= start + ignoreFirstPairs);
			if (consideredPairs.length === 0) consideredPairs = pairResults;

			let passed = consideredPairs.every((item) => item.errorRatio <= maxErrorRatio);
			if (ctype === 'telemetry_direction_match') {
				const minCompatibleRatio = parseFloat(check.minCompatibleRatio ?? 0.7);
				const compatibleCount = consideredPairs.filter((item) => item.compatible).length;
				const pairCount = Math.max(1, consideredPairs.length);
				passed = compatibleCount / pairCount >= minCompatibleRatio;
			}

			const entry: any = {
				scope: segmentName,
				type: ctype,
				name: check.name ?? `${segmentName}:${ctype}`,
				range: [start, end],
				roi: { x: roiAbs[0], y: roiAbs[1], w: roiAbs[2], h: roiAbs[3] },
				rgbTolerance: tolerance,
				maxErrorRatio,
				ignoreFirstPairs,
				worstErrorRatio: worstRatio,
				worstPair,
				passed,
				pairs: pairResults,
				consideredPairCount: consideredPairs.length,
			};
			if (ctype === 'telemetry_direction_match') {
				const compatibleCount = consideredPairs.filter((item) => item.compatible).length;
				const pairCount = Math.max(1, consideredPairs.length);
				entry.compatiblePairs = compatibleCount;
				entry.pairCount = pairCount;
				entry.compatibleRatio = compatibleCount / pairCount;
				entry.minCompatibleRatio = parseFloat(check.minCompatibleRatio ?? 0.7);
			}
			checksReport.push(entry);
			if (!passed) {
				if (ctype === 'telemetry_direction_match') {
					for (const pairItem of pairResults) {
						if (pairItem.compatible) continue;
						const overlayName =
							`${segmentName}_${ctype}_f${String(pairItem.from).padStart(3, '0')}_f${String(pairItem.to).padStart(3, '0')}.png`;
						const overlayPath = path.join(overlaysDir, overlayName);
						writeMismatchOverlay(frameImages[pairItem.from], frameImages[pairItem.to], roiAbs,
							parseInt(pairItem.observedDx, 10), parseInt(pairItem.observedDy, 10), tolerance, overlayPath);
						pairItem.overlay = overlayPath;
					}
					failures.push({
						name: entry.name,
						reason: `compatibleRatio=${entry.compatibleRatio.toFixed(6)} < minCompatibleRatio=${entry.minCompatibleRatio.toFixed(6)}`,
						frame: worstPair[0],
					});
					continue;
				}
				for (const pairItem of pairResults) {
					if (pairItem.from < start + ignoreFirstPairs) continue;
					if (pairItem.errorRatio <= maxErrorRatio) continue;
					const overlayName =
						`${segmentName}_${ctype}_f${String(pairItem.from).padStart(3, '0')}_f${String(pairItem.to).padStart(3, '0')}.png`;
					const overlayPath = path.join(overlaysDir, overlayName);
					writeMismatchOverlay(frameImages[pairItem.from], frameImages[pairItem.to], roiAbs,
						parseInt(pairItem.dx ?? 0, 10), parseInt(pairItem.dy ?? 0, 10), tolerance, overlayPath);
					pairItem.overlay = overlayPath;
				}
				failures.push({
					name: entry.name,
					reason: `worstErrorRatio=${worstRatio.toFixed(6)} > maxErrorRatio=${maxErrorRatio.toFixed(6)}`,
					frame: worstPair[0],
				});
			}
		}
	}

	const status = failures.length === 0 ? 'ok' : 'failed';
	return {
		status,
		frames: frames.length,
		viewport: { x: viewport.x, y: viewport.y, w: viewport.w, h: viewport.h },
		contractVersion: contract.version ?? 1,
		checks: checksReport,
		failures,
		telemetryFrames: telemetry.length,
	};
}

function buildSummary(report: any, sequenceDir: string, contractPath: string): string {
	const lines: string[] = [];
	lines.push('# Pixel Assert Summary');
	lines.push('');
	lines.push(`Status: ${report.status}`);
	lines.push(`Frames: ${report.frames}`);
	lines.push(`SequenceDir: ${sequenceDir}`);
	lines.push(`Contract: ${contractPath}`);
	const viewport = report.viewport;
	lines.push(`Viewport: x=${viewport.x} y=${viewport.y} w=${viewport.w} h=${viewport.h}`);
	lines.push(`TelemetryFrames: ${report.telemetryFrames ?? 0}`);
	lines.push('');
	lines.push('## Checks');
	for (const check of report.checks) {
		const state = check.passed ? 'OK' : 'FAIL';
		const worst = check.worstErrorRatio ?? check.maxSeenRatio ?? 0.0;
		lines.push(`- [${state}] ${check.name} (${check.type}) worst=${worst.toFixed(6)}`);
	}
	lines.push('');
	lines.push('## Failures');
	if (report.failures.length === 0) {
		lines.push('- none');
	} else {
		for (const failure of report.failures) {
			lines.push(`- frame ${failure.frame}: ${failure.name} -> ${failure.reason}`);
		}
	}
	lines.push('');
	return lines.join('\n');
}

function main(): void {
	const args = process.argv.slice(2);
	const sequenceDirValue = argValue(args, '--sequence-dir');
	const contractValue = argValue(args, '--contract');
	const runReportValue = argValue(args, '--run-report');
	const outDirValue = argValue(args, '--out-dir');
	if (!sequenceDirValue || !contractValue) {
		fail('Uso: assert_pixel_contract.js --sequence-dir <dir> --contract <pixel-contract.json> [--run-report] [--out-dir]');
	}
	const sequenceDir = path.resolve(sequenceDirValue);
	const contractPath = path.resolve(contractValue);
	if (!fs.existsSync(sequenceDir) || !fs.statSync(sequenceDir).isDirectory()) {
		fail(`SequenceDir invalido: ${sequenceDir}`);
	}
	if (!fs.existsSync(contractPath)) {
		fail(`Contract no encontrado: ${contractPath}`);
	}
	const outDir = outDirValue ? path.resolve(outDirValue) : sequenceDir;
	fs.mkdirSync(outDir, { recursive: true });

	const contract = readJson(contractPath);
	const frames = listFrames(sequenceDir);
	const viewport = resolveViewport(contract, frames[0]);
	const coords = resolveCoordinateSpace(contract, viewport);
	const overlaysDir = path.join(outDir, 'overlays');
	const report = runAssertions(frames, contract, viewport, coords, runReportValue ?? null, overlaysDir);

	const reportPath = path.join(outDir, 'pixel-assert-report.json');
	const summaryPath = path.join(outDir, 'pixel-assert-summary.md');
	fs.writeFileSync(reportPath, JSON.stringify(report, null, 2), 'utf-8');
	fs.writeFileSync(summaryPath, buildSummary(report, sequenceDir, contractPath), 'utf-8');

	console.log(`Status: ${report.status}`);
	console.log(`Report: ${reportPath}`);
	console.log(`Summary: ${summaryPath}`);

	if (report.status !== 'ok') process.exit(1);
}

main();
