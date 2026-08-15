#!/usr/bin/env node
/**
 * Analizador de secuencias de frames: huellas de luminancia, diferencia entre
 * pares y hoja de contacto. Sustituye a analyze_frame_sequence.py (Pillow) por
 * una version Node/pngjs.
 *
 * Uso: node dist/tools/analyze/analyze_frame_sequence.js <carpeta>
 *      [--min-frames N] [--expect-animated|--expect-static]
 *      [--diff-threshold N] [--out-dir <dir>]
 */
import * as fs from 'fs';
import * as path from 'path';
import { argValue, hasFlag, fail } from '../lib/cli.js';
import { readPng, createImage, savePng, copyRect, pixel, drawText } from '../lib/image.js';
const GRID_W = 8;
const GRID_H = 8;
const SAMPLE_W = 64;
const SAMPLE_H = 48;
/** Calcula la luminancia media y un hash 8x8 de la imagen. */
function frameFingerprint(framePath) {
    const image = readPng(framePath);
    const { width, height, data } = image;
    const grid = [];
    let total = 0;
    let samples = 0;
    for (let gy = 0; gy < GRID_H; ++gy) {
        for (let gx = 0; gx < GRID_W; ++gx) {
            const x = Math.min(width - 1, Math.floor((gx + 0.5) * width / GRID_W));
            const y = Math.min(height - 1, Math.floor((gy + 0.5) * height / GRID_H));
            const [r, g, b] = pixel(image, x, y);
            const luma = 0.299 * r + 0.587 * g + 0.114 * b;
            grid.push(luma);
            total += luma;
            samples++;
        }
    }
    const mean = total / Math.max(1, samples);
    const hash = grid.map((luma) => (luma >= mean ? '1' : '0')).join('');
    return { path: framePath, width, height, hash, meanLuma: Math.round(mean * 1000) / 1000 };
}
/** Compara dos frames muestreando 64x48 puntos y devuelve el diff medio. */
function comparePair(aPath, bPath, threshold) {
    const a = readPng(aPath);
    const b = readPng(bPath);
    let totalDiff = 0;
    let changed = 0;
    let samples = 0;
    for (let sy = 0; sy < SAMPLE_H; ++sy) {
        for (let sx = 0; sx < SAMPLE_W; ++sx) {
            const xa = Math.min(a.width - 1, Math.floor((sx + 0.5) * a.width / SAMPLE_W));
            const ya = Math.min(a.height - 1, Math.floor((sy + 0.5) * a.height / SAMPLE_H));
            const xb = Math.min(b.width - 1, Math.floor((sx + 0.5) * b.width / SAMPLE_W));
            const yb = Math.min(b.height - 1, Math.floor((sy + 0.5) * b.height / SAMPLE_H));
            const [ra, ga, ba] = pixel(a, xa, ya);
            const [rb, gb, bb] = pixel(b, xb, yb);
            const diff = (Math.abs(ra - rb) + Math.abs(ga - gb) + Math.abs(ba - bb)) / 3.0;
            totalDiff += diff;
            if (diff > threshold)
                changed++;
            samples++;
        }
    }
    return {
        from: path.basename(aPath),
        to: path.basename(bPath),
        meanDiff: Math.round((totalDiff / Math.max(1, samples)) * 1000) / 1000,
        changedSamples: changed,
        changedRatio: Math.round((changed / Math.max(1, samples)) * 10000) / 10000,
    };
}
/** Compone una hoja de contacto con miniaturas y etiquetas de diff. */
function buildContactSheet(frames, pairs, outPath) {
    const thumbW = 160;
    const thumbH = 128;
    const columns = Math.max(1, Math.min(4, frames.length));
    const rows = Math.ceil(frames.length / columns);
    const sheet = createImage(thumbW * columns, thumbH * rows, [24, 24, 24]);
    for (let i = 0; i < frames.length; ++i) {
        const col = i % columns;
        const row = Math.floor(i / columns);
        const x = col * thumbW;
        const y = row * thumbH;
        const src = readPng(frames[i]);
        const copyW = Math.min(thumbW, src.width);
        const copyH = Math.min(thumbH - 16, src.height);
        copyRect(src, sheet, 0, 0, x, y + 16, copyW, copyH);
        let label = path.basename(frames[i]);
        if (i > 0) {
            const pair = pairs[i - 1];
            label = `${label} diff=${pair.meanDiff} ch=${pair.changedSamples}`;
        }
        drawText(sheet, x + 4, y + 2, label, [255, 255, 255]);
    }
    savePng(sheet, outPath);
}
function main() {
    const args = process.argv.slice(2);
    if (args.length < 1) {
        fail('Uso: analyze_frame_sequence.js <carpeta> [opciones]');
    }
    const sequenceDir = path.resolve(args[0]);
    if (!fs.existsSync(sequenceDir) || !fs.statSync(sequenceDir).isDirectory()) {
        fail(`No existe la carpeta de secuencia: ${sequenceDir}`);
    }
    const minFrames = parseInt(argValue(args, '--min-frames', '2'), 10);
    const diffThreshold = parseInt(argValue(args, '--diff-threshold', '3'), 10);
    const expectAnimated = hasFlag(args, '--expect-animated');
    const expectStatic = hasFlag(args, '--expect-static');
    const outDirValue = argValue(args, '--out-dir');
    const outDir = outDirValue ? path.resolve(outDirValue) : sequenceDir;
    const frames = fs.readdirSync(sequenceDir)
        .filter((name) => name.startsWith('frame_') && name.endsWith('.png'))
        .sort()
        .map((name) => path.join(sequenceDir, name));
    if (frames.length < minFrames) {
        fail(`La secuencia contiene ${frames.length} frames, pero se esperaban al menos ${minFrames}.`);
    }
    fs.mkdirSync(outDir, { recursive: true });
    const fingerprints = frames.map(frameFingerprint);
    const pairs = [];
    for (let i = 1; i < frames.length; ++i) {
        pairs.push(comparePair(frames[i - 1], frames[i], diffThreshold));
    }
    const changedPairs = pairs.filter((p) => p.changedSamples > 0);
    const duplicatePairs = pairs.filter((p) => p.changedSamples === 0);
    const meanDiffAvg = pairs.length
        ? Math.round((pairs.reduce((sum, p) => sum + p.meanDiff, 0) / pairs.length) * 1000) / 1000
        : 0;
    const maxDiff = pairs.length ? Math.max(...pairs.map((p) => p.meanDiff)) : 0;
    const contactSheet = path.join(outDir, 'contact-sheet.png');
    buildContactSheet(frames, pairs, contactSheet);
    let status = 'ok';
    if (expectAnimated && changedPairs.length === 0)
        status = 'expected_animation_but_frames_are_static';
    if (expectStatic && changedPairs.length > 0)
        status = 'expected_static_but_frames_changed';
    const result = {
        status,
        frames: frames.length,
        duplicatePairs: duplicatePairs.length,
        changedPairs: changedPairs.length,
        meanDiffAvg,
        maxDiff,
        diffThreshold,
        contactSheet,
        fingerprints,
        pairs,
    };
    fs.writeFileSync(path.join(outDir, 'sequence-analysis.json'), JSON.stringify(result, null, 2), 'utf-8');
    console.log(`Status=${status} Frames=${frames.length} DuplicatePairs=${duplicatePairs.length} ` +
        `ChangedPairs=${changedPairs.length} MeanDiffAvg=${meanDiffAvg} MaxDiff=${maxDiff} ` +
        `ContactSheet=${contactSheet}`);
    if (status !== 'ok')
        process.exit(1);
}
main();
