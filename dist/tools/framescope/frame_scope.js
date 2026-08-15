#!/usr/bin/env node
/**
 * FrameScope: analisis visual temporal determinista de secuencias de frames o
 * videos. Sustituye a frame_scope.py (Pillow/numpy) por una version Node/pngjs.
 *
 * Cada frame se reduce a una rejilla de luminancia/color, se estima el
 * desplazamiento global entre pares y se escribe un informe JSON, un resumen
 * Markdown y una hoja de contacto. El perfil `amiga-scroll` anade la correlacion
 * con la telemetria lateral `g_eng_run_status` de run-report.json.
 *
 * Uso: node dist/tools/framescope/frame_scope.js --source <carpeta|video>
 *      [--out-dir <dir>] [--fps 12] [--max-frames 120] [--grid-width 32]
 *      [--grid-height 24] [--search-radius 4] [--diff-threshold 6]
 *      [--profile generic|amiga-scroll] [--run-report <ruta>]
 *      [--max-profile-mismatches 0] [--expect-animated]
 *      [--require-profile-match] [--keep-extracted-frames]
 */
import * as fs from 'fs';
import * as path from 'path';
import { spawnSync } from 'child_process';
import { argValue, hasFlag, fail } from '../lib/cli.js';
import { repoRoot } from '../lib/paths.js';
import { readPng, createImage, savePng, copyRect, drawText } from '../lib/image.js';
const root = repoRoot(import.meta.url);
const VIDEO_EXTENSIONS = ['.mp4', '.mkv', '.mov', '.avi', '.webm', '.mpg', '.mpeg', '.m4v'];
const IMAGE_EXTENSIONS = ['.png', '.jpg', '.jpeg', '.bmp'];
function isVideo(filePath) {
    return VIDEO_EXTENSIONS.includes(path.extname(filePath).toLowerCase());
}
function imageFiles(sequenceDir) {
    const files = fs.readdirSync(sequenceDir)
        .filter((name) => IMAGE_EXTENSIONS.includes(path.extname(name).toLowerCase()))
        .sort();
    const frameNamed = files.filter((name) => name.startsWith('frame_'));
    return (frameNamed.length ? frameNamed : files).map((name) => path.join(sequenceDir, name));
}
/** Extrae frames de un video con ffmpeg a un directorio de frames. */
function expandVideoFrames(video, outDir, fps, limit, keep) {
    if (!spawnSync('sh', ['-c', 'command -v ffmpeg']).status) {
        fail('FrameScope necesita ffmpeg para leer video. Instala ffmpeg o pasa una carpeta de frames PNG/JPG.');
    }
    const framesDir = path.join(outDir, 'extracted-frames');
    fs.mkdirSync(framesDir, { recursive: true });
    const pattern = path.join(framesDir, 'frame_%05d.png');
    const result = spawnSync('ffmpeg', ['-y', '-i', video, '-vf', `fps=${fps}`, '-frames:v', String(limit), pattern], { encoding: 'utf-8' });
    if (result.status !== 0) {
        fail(`ffmpeg no pudo extraer frames de ${video}: ${(result.stderr || '').slice(-500)}`);
    }
    return framesDir;
}
function colorSymbol(r, g, b) {
    const luma = 0.299 * r + 0.587 * g + 0.114 * b;
    if (luma < 24)
        return ' ';
    if (luma > 230)
        return 'W';
    if (r > 180 && g > 160 && b < 120)
        return 'Y';
    if (r > 180 && g > 80 && b < 100)
        return 'O';
    if (r > 170 && g < 120 && b < 120)
        return 'R';
    if (g > 150 && r < 140 && b < 140)
        return 'G';
    if (b > 150 && r < 140 && g < 170)
        return 'B';
    if (g > 140 && b > 140 && r < 140)
        return 'C';
    if (r > 140 && b > 140 && g < 140)
        return 'M';
    return '.';
}
/** Localiza el rectangulo de contenido (no negro) del primer frame. */
function contentBounds(filePath, threshold = 8) {
    const image = readPng(filePath);
    const { width, height, data } = image;
    let left = width;
    let right = -1;
    let top = height;
    let bottom = -1;
    for (let y = 0; y < height; ++y) {
        for (let x = 0; x < width; ++x) {
            const i = (y * width + x) * 4;
            const luma = 0.299 * data[i] + 0.587 * data[i + 1] + 0.114 * data[i + 2];
            if (luma > threshold) {
                if (x < left)
                    left = x;
                if (x > right)
                    right = x;
                if (y < top)
                    top = y;
                if (y > bottom)
                    bottom = y;
            }
        }
    }
    if (right < left || bottom < top) {
        return { left: 0, top: 0, width, height };
    }
    return { left, top, width: right - left + 1, height: bottom - top + 1 };
}
function frameMetrics(filePath, index, gridW, gridH, crop) {
    const image = readPng(filePath);
    const { width, height, data } = image;
    let sampleLeft = 0;
    let sampleTop = 0;
    let sampleWidth = width;
    let sampleHeight = height;
    if (crop !== null) {
        sampleLeft = Math.max(0, crop.left);
        sampleTop = Math.max(0, crop.top);
        sampleWidth = Math.min(width - sampleLeft, crop.width);
        sampleHeight = Math.min(height - sampleTop, crop.height);
    }
    const lumaGrid = [];
    const symbols = [];
    let totalR = 0;
    let totalG = 0;
    let totalB = 0;
    let totalLuma = 0;
    for (let gy = 0; gy < gridH; ++gy) {
        const rowLuma = [];
        const rowSymbols = [];
        for (let gx = 0; gx < gridW; ++gx) {
            const x = Math.min(width - 1, sampleLeft + Math.floor((gx + 0.5) * sampleWidth / gridW));
            const y = Math.min(height - 1, sampleTop + Math.floor((gy + 0.5) * sampleHeight / gridH));
            const i = (y * width + x) * 4;
            const r = data[i];
            const g = data[i + 1];
            const b = data[i + 2];
            const luma = 0.299 * r + 0.587 * g + 0.114 * b;
            rowLuma.push(luma);
            rowSymbols.push(colorSymbol(r, g, b));
            totalR += r;
            totalG += g;
            totalB += b;
            totalLuma += luma;
        }
        lumaGrid.push(rowLuma);
        symbols.push(rowSymbols.join(''));
    }
    const count = Math.max(1, gridW * gridH);
    const meanLuma = totalLuma / count;
    const signature = lumaGrid.flat().map((cell) => (cell >= meanLuma ? '1' : '0')).join('');
    return {
        index, path: filePath, file: path.basename(filePath),
        width, height,
        meanR: Math.round((totalR / count) * 1000) / 1000,
        meanG: Math.round((totalG / count) * 1000) / 1000,
        meanB: Math.round((totalB / count) * 1000) / 1000,
        meanLuma: Math.round(meanLuma * 1000) / 1000,
        lumaGrid, symbolGrid: symbols, signature,
    };
}
function shiftError(a, b, dx, dy) {
    let total = 0;
    let count = 0;
    for (let y = 0; y < a.length; ++y) {
        const by = y + dy;
        if (by < 0 || by >= a.length)
            continue;
        for (let x = 0; x < a[0].length; ++x) {
            const bx = x + dx;
            if (bx < 0 || bx >= a[0].length)
                continue;
            total += Math.abs(a[y][x] - b[by][bx]);
            count++;
        }
    }
    return count ? total / count : Infinity;
}
function directionName(dx, dy) {
    if (dx === 0 && dy === 0)
        return 'static';
    const horizontal = dx > 0 ? 'right' : dx < 0 ? 'left' : '';
    const vertical = dy > 0 ? 'down' : dy < 0 ? 'up' : '';
    if (horizontal && vertical)
        return `${vertical}-${horizontal}`;
    return horizontal || vertical;
}
function compareMetrics(a, b, gridW, gridH, radius, threshold) {
    let totalDiff = 0;
    let changed = 0;
    const quadrants = { topLeft: 0, topRight: 0, bottomLeft: 0, bottomRight: 0 };
    const quadrantCounts = { topLeft: 0, topRight: 0, bottomLeft: 0, bottomRight: 0 };
    for (let y = 0; y < gridH; ++y) {
        for (let x = 0; x < gridW; ++x) {
            const diff = Math.abs(a.lumaGrid[y][x] - b.lumaGrid[y][x]);
            totalDiff += diff;
            if (diff > threshold)
                changed++;
            const key = y < gridH / 2 ? (x < gridW / 2 ? 'topLeft' : 'topRight') : (x < gridW / 2 ? 'bottomLeft' : 'bottomRight');
            quadrants[key] += diff;
            quadrantCounts[key]++;
        }
    }
    let bestDx = 0;
    let bestDy = 0;
    let bestError = Infinity;
    const candidates = [];
    for (let dy = -radius; dy <= radius; ++dy) {
        for (let dx = -radius; dx <= radius; ++dx) {
            const error = shiftError(a.lumaGrid, b.lumaGrid, dx, dy);
            candidates.push({ dx, dy, error, direction: directionName(dx, dy) });
            if (error < bestError) {
                bestError = error;
                bestDx = dx;
                bestDy = dy;
            }
        }
    }
    const nearLimit = Math.max(bestError + 3.0, bestError * 1.06);
    const nearDirections = [];
    const seen = new Set();
    for (const candidate of [...candidates].sort((c1, c2) => c1.error - c2.error)) {
        if (candidate.error <= nearLimit && !seen.has(candidate.direction)) {
            seen.add(candidate.direction);
            nearDirections.push(candidate.direction);
        }
        if (nearDirections.length >= 8)
            break;
    }
    const samples = Math.max(1, gridW * gridH);
    const roundedQuadrants = {};
    for (const key of Object.keys(quadrants)) {
        roundedQuadrants[key] = Math.round((quadrants[key] / Math.max(1, quadrantCounts[key])) * 1000) / 1000;
    }
    return {
        from: a.index, to: b.index,
        meanDiff: Math.round((totalDiff / samples) * 1000) / 1000,
        changedCells: changed,
        changedRatio: Math.round((changed / samples) * 10000) / 10000,
        contentShiftDx: bestDx, contentShiftDy: bestDy,
        contentDirection: directionName(bestDx, bestDy),
        candidateDirections: nearDirections,
        shiftError: Math.round(bestError * 1000) / 1000,
        quadrants: roundedQuadrants,
    };
}
function motionSegments(pairs) {
    const segments = [];
    if (pairs.length === 0)
        return segments;
    let currentDirection = pairs[0].contentDirection;
    let start = pairs[0].from;
    let diffTotal = 0;
    let count = 0;
    for (const pair of pairs) {
        if (pair.contentDirection !== currentDirection) {
            segments.push({
                startFrame: start, endFrame: pair.from, direction: currentDirection,
                frames: count, meanDiff: Math.round((diffTotal / Math.max(1, count)) * 1000) / 1000,
            });
            currentDirection = pair.contentDirection;
            start = pair.from;
            diffTotal = 0;
            count = 0;
        }
        diffTotal += pair.meanDiff;
        count++;
    }
    segments.push({
        startFrame: start, endFrame: pairs[pairs.length - 1].to, direction: currentDirection,
        frames: count, meanDiff: Math.round((diffTotal / Math.max(1, count)) * 1000) / 1000,
    });
    return segments;
}
function decodeAmigaRunStatus(runStatus) {
    if (!runStatus || !runStatus.ok)
        return null;
    const detail = parseInt(runStatus.detail ?? 0, 10);
    return {
        ok: true,
        frame: parseInt(runStatus.frame ?? 0, 10),
        detail: `0x${(detail >>> 0).toString(16).padStart(8, '0')}`,
        cameraX: (detail >> 16) & 0xff,
        cameraY: (detail >> 8) & 0xff,
        tileJobs: (detail >> 4) & 0x0f,
        prefetchFlags: detail & 0x0f,
    };
}
function expectedContentDirection(deltaX, deltaY) {
    if (Math.abs(deltaX) > Math.abs(deltaY))
        deltaY = 0;
    else if (Math.abs(deltaY) > Math.abs(deltaX))
        deltaX = 0;
    const horizontal = deltaX > 0 ? 'left' : deltaX < 0 ? 'right' : '';
    const vertical = deltaY > 0 ? 'up' : deltaY < 0 ? 'down' : '';
    if (horizontal && vertical)
        return `${vertical}-${horizontal}`;
    return horizontal || vertical || 'static';
}
function directionCompatible(observed, expected, candidates) {
    if (expected === 'static')
        return observed === 'static';
    for (const direction of [observed, ...candidates]) {
        const parts = expected.split('-');
        if (parts.every((part) => direction.includes(part)))
            return true;
    }
    return false;
}
function amigaScrollProfile(reportPath, pairs, allowedMismatches) {
    if (!reportPath) {
        return {
            status: 'missing_run_report', observations: [], mismatches: [],
            message: 'No se encontro run-report.json para correlacionar telemetria Amiga.',
        };
    }
    const runReport = JSON.parse(fs.readFileSync(reportPath, 'utf-8'));
    const frames = (runReport.sequence ?? {}).frames ?? [];
    const telemetry = [];
    for (let i = 0; i < frames.length; ++i) {
        const decoded = decodeAmigaRunStatus(frames[i].runStatus);
        if (decoded !== null) {
            decoded.sequenceIndex = i;
            telemetry.push(decoded);
        }
    }
    const observations = [];
    const mismatches = [];
    for (let i = 1; i < telemetry.length; ++i) {
        if (i - 1 >= pairs.length)
            break;
        const prev = telemetry[i - 1];
        const cur = telemetry[i];
        const pair = pairs[i - 1];
        const dx = cur.cameraX - prev.cameraX;
        const dy = cur.cameraY - prev.cameraY;
        const expected = expectedContentDirection(dx, dy);
        const compatible = directionCompatible(pair.contentDirection, expected, pair.candidateDirections);
        const observation = {
            from: pair.from, to: pair.to,
            programFrameFrom: prev.frame, programFrameTo: cur.frame,
            cameraFrom: `${prev.cameraX},${prev.cameraY}`, cameraTo: `${cur.cameraX},${cur.cameraY}`,
            cameraDeltaX: dx, cameraDeltaY: dy,
            expectedContentDirection: expected,
            observedContentDirection: pair.contentDirection,
            candidateDirections: pair.candidateDirections,
            observedShift: `${pair.contentShiftDx},${pair.contentShiftDy}`,
            meanDiff: pair.meanDiff,
            compatible,
        };
        observations.push(observation);
        if (!compatible)
            mismatches.push(observation);
    }
    let status;
    if (telemetry.length < 2)
        status = 'insufficient_telemetry';
    else if (mismatches.length > allowedMismatches)
        status = 'mismatch';
    else if (mismatches.length > 0)
        status = 'ok_with_tolerance';
    else
        status = 'ok';
    return {
        status, runReport: reportPath, telemetryFrames: telemetry.length,
        allowedMismatches, observations, mismatches,
    };
}
/** Compone la hoja de contacto con miniaturas y etiquetas de movimiento. */
function contactSheet(frames, pairs, outPath) {
    const thumbW = 192;
    const thumbH = 154;
    const columns = Math.max(1, Math.min(4, frames.length));
    const rows = Math.ceil(frames.length / columns);
    const sheet = createImage(thumbW * columns, thumbH * rows, [20, 20, 20]);
    for (let i = 0; i < frames.length; ++i) {
        const col = i % columns;
        const row = Math.floor(i / columns);
        const x = col * thumbW;
        const y = row * thumbH;
        const src = readPng(frames[i]);
        const copyW = Math.min(thumbW, src.width);
        const copyH = Math.min(thumbH - 26, src.height);
        copyRect(src, sheet, 0, 0, x, y + 26, copyW, copyH);
        let label = `f${i} ${path.basename(frames[i])}`;
        if (i > 0) {
            const pair = pairs[i - 1];
            label = `f${i} d=${pair.meanDiff} ${pair.contentDirection} (${pair.contentShiftDx},${pair.contentShiftDy})`;
        }
        drawText(sheet, x + 4, y + 4, label, [255, 255, 255]);
    }
    savePng(sheet, outPath);
}
function main() {
    const args = process.argv.slice(2);
    const sourceValue = argValue(args, '--source');
    if (!sourceValue) {
        fail('Uso: frame_scope.js --source <carpeta|video> [opciones]');
    }
    const source = path.resolve(sourceValue);
    const requestedOut = argValue(args, '--out-dir');
    const outDir = requestedOut
        ? path.resolve(requestedOut)
        : path.join(root, 'out/framescope', `${path.basename(source)}-${new Date().toISOString().replace(/[-:T.]/g, '').slice(0, 14)}`);
    fs.mkdirSync(outDir, { recursive: true });
    const fps = parseInt(argValue(args, '--fps', '12'), 10);
    const maxFrames = parseInt(argValue(args, '--max-frames', '120'), 10);
    const gridW = parseInt(argValue(args, '--grid-width', '32'), 10);
    const gridH = parseInt(argValue(args, '--grid-height', '24'), 10);
    const radius = parseInt(argValue(args, '--search-radius', '4'), 10);
    const threshold = parseInt(argValue(args, '--diff-threshold', '6'), 10);
    const profile = argValue(args, '--profile', 'generic') ?? 'generic';
    const runReport = argValue(args, '--run-report', '');
    const maxMismatches = parseInt(argValue(args, '--max-profile-mismatches', '0'), 10);
    const expectAnimated = hasFlag(args, '--expect-animated');
    const requireProfileMatch = hasFlag(args, '--require-profile-match');
    const keepFrames = hasFlag(args, '--keep-extracted-frames');
    let sequenceDir;
    let temporaryFrames = false;
    if (fs.existsSync(source) && fs.statSync(source).isFile() && isVideo(source)) {
        sequenceDir = expandVideoFrames(source, outDir, fps, maxFrames, keepFrames);
        temporaryFrames = !keepFrames;
    }
    else if (fs.existsSync(source) && fs.statSync(source).isDirectory()) {
        sequenceDir = source;
    }
    else {
        fail(`FrameScope espera una carpeta de frames o un video soportado: ${source}`);
    }
    let frames = imageFiles(sequenceDir);
    if (frames.length === 0)
        fail(`No se encontraron frames PNG/JPG/BMP en ${sequenceDir}.`);
    frames = frames.slice(0, maxFrames);
    const autoCrop = profile === 'amiga-scroll';
    const crop = autoCrop ? contentBounds(frames[0]) : null;
    const metrics = frames.map((f, i) => frameMetrics(f, i, gridW, gridH, crop));
    const pairs = [];
    for (let i = 1; i < metrics.length; ++i) {
        pairs.push(compareMetrics(metrics[i - 1], metrics[i], gridW, gridH, radius, threshold));
    }
    const segments = motionSegments(pairs);
    let status = 'ok';
    if (expectAnimated && !pairs.some((p) => p.changedCells > 0)) {
        status = 'expected_animation_but_sequence_is_static';
    }
    let profileReport = null;
    if (profile === 'amiga-scroll') {
        let resolvedReport = runReport;
        if (!resolvedReport) {
            const candidate = path.join(path.dirname(sequenceDir), 'run-report.json');
            if (fs.existsSync(candidate))
                resolvedReport = candidate;
        }
        profileReport = amigaScrollProfile(resolvedReport ?? '', pairs, maxMismatches);
        if (requireProfileMatch && profileReport.status !== 'ok' && profileReport.status !== 'ok_with_tolerance') {
            status = `profile_${profileReport.status}`;
        }
    }
    const sheetPath = path.join(outDir, 'framescope-contact-sheet.png');
    contactSheet(frames, pairs, sheetPath);
    const compactFrames = metrics.map((m) => ({
        index: m.index, file: m.file, width: m.width, height: m.height,
        meanR: m.meanR, meanG: m.meanG, meanB: m.meanB, meanLuma: m.meanLuma,
        symbolGrid: m.symbolGrid, signature: m.signature,
    }));
    const summaryLines = [
        '# FrameScope summary',
        '',
        `Input: ${source}`,
        `Frames: ${metrics.length}`,
        `Status: ${status}`,
        `ContactSheet: ${sheetPath}`,
    ];
    if (crop !== null) {
        summaryLines.push(`Crop: left=${crop.left}, top=${crop.top}, width=${crop.width}, height=${crop.height}`);
    }
    summaryLines.push('');
    summaryLines.push('## Motion segments');
    for (const segment of segments) {
        summaryLines.push(`- frames ${segment.startFrame}..${segment.endFrame}: ${segment.direction}, samples=${segment.frames}, meanDiff=${segment.meanDiff}`);
    }
    if (profileReport !== null) {
        summaryLines.push('');
        summaryLines.push('## Profile amiga-scroll');
        summaryLines.push(`Status: ${profileReport.status}`);
        summaryLines.push(`TelemetryFrames: ${profileReport.telemetryFrames}`);
        for (const observation of profileReport.observations) {
            const mark = observation.compatible ? 'OK' : 'MISMATCH';
            const candidates = observation.candidateDirections.length
                ? `, candidates=${observation.candidateDirections.join('/')}` : '';
            summaryLines.push(`- ${mark} frames ${observation.from}..${observation.to}: cam ${observation.cameraFrom} -> ${observation.cameraTo}, ` +
                `expected=${observation.expectedContentDirection}, observed=${observation.observedContentDirection}, ` +
                `shift=${observation.observedShift}, diff=${observation.meanDiff}${candidates}`);
        }
    }
    summaryLines.push('');
    summaryLines.push('## Frame grids');
    for (const frame of compactFrames) {
        summaryLines.push(`### frame ${frame.index} ${frame.file} luma=${frame.meanLuma}`);
        summaryLines.push(...frame.symbolGrid);
        summaryLines.push('');
    }
    const report = {
        status, input: source, outputDir: outDir, sequenceDir,
        frames: metrics.length, gridWidth: gridW, gridHeight: gridH,
        searchRadius: radius, diffThreshold: threshold, cropBounds: crop,
        contactSheet: sheetPath, profile, profileReport, segments, pairs, frameMetrics: compactFrames,
    };
    const jsonPath = path.join(outDir, 'framescope-report.json');
    const summaryPath = path.join(outDir, 'framescope-summary.md');
    fs.writeFileSync(jsonPath, JSON.stringify(report, null, 2), 'utf-8');
    fs.writeFileSync(summaryPath, summaryLines.join('\n') + '\n', 'utf-8');
    if (temporaryFrames && fs.existsSync(sequenceDir)) {
        fs.rmSync(sequenceDir, { recursive: true, force: true });
    }
    console.log(`Status=${status} Frames=${metrics.length} Segments=${segments.length} ` +
        `ContactSheet=${sheetPath} Json=${jsonPath} Summary=${summaryPath}`);
    if (status !== 'ok')
        process.exit(1);
}
main();
