#!/usr/bin/env node
/**
 * Selftest del motor de aserciones de pixeles (assert_pixel_contract.js).
 *
 * Genera bitmaps sinteticos que deben pasar o fallar cada tipo de check y
 * verifica que el motor se comporta como se espera. Sustituye a
 * verify_pixel_assert.py (Pillow) por una version Node/pngjs.
 *
 * Uso: node dist/tools/analyze/verify_pixel_assert.js [--out-dir <dir>]
 */
import * as fs from 'fs';
import * as path from 'path';
import { spawnSync } from 'child_process';
import { argValue } from '../lib/cli.js';
import { repoRoot } from '../lib/paths.js';
import { createImage, savePng, setPixel } from '../lib/image.js';
const root = repoRoot(import.meta.url);
function solidBitmap(outPath, color) {
    const image = createImage(64, 64, color);
    savePng(image, outPath);
}
function copyFrame(src, dst) {
    fs.copyFileSync(src, dst);
}
/** Crea dos frames con un rectangulo desplazado 2px a la derecha. */
function shiftedRects(sequenceDir) {
    const bg = [20, 20, 20];
    const fg = [220, 180, 40];
    const img0 = createImage(64, 64, bg);
    const img1 = createImage(64, 64, bg);
    for (let y = 16; y <= 36; ++y) {
        for (let x = 12; x <= 36; ++x)
            setPixel(img0, x, y, fg[0], fg[1], fg[2]);
        for (let x = 14; x <= 38; ++x)
            setPixel(img1, x, y, fg[0], fg[1], fg[2]);
    }
    savePng(img0, path.join(sequenceDir, 'frame_000.png'));
    savePng(img1, path.join(sequenceDir, 'frame_001.png'));
}
/** Crea dos frames: uno limpio y otro con un bloque negro prohibido. */
function forbiddenBlack(sequenceDir) {
    const bg = [32, 96, 160];
    solidBitmap(path.join(sequenceDir, 'frame_000.png'), bg);
    const image = createImage(64, 64, bg);
    for (let y = 8; y <= 32; ++y) {
        for (let x = 8; x <= 32; ++x)
            setPixel(image, x, y, 0, 0, 0);
    }
    savePng(image, path.join(sequenceDir, 'frame_001.png'));
}
const BASE_CONTRACT = {
    version: 1,
    viewport: { mode: 'fixed', x: 0, y: 0, w: 64, h: 64, logicalWidth: 64, logicalHeight: 64 },
    defaults: { rgbTolerance: 0, maxErrorRatio: 0.0 },
    globalChecks: [],
    segments: [],
};
function invokeCase(name, arrange, contract, expectedPass, suiteDir) {
    const caseDir = path.join(suiteDir, name);
    const sequenceDir = path.join(caseDir, 'sequence');
    const reportDir = path.join(caseDir, 'report');
    fs.mkdirSync(sequenceDir, { recursive: true });
    fs.mkdirSync(reportDir, { recursive: true });
    arrange(sequenceDir);
    const contractPath = path.join(caseDir, 'pixel-contract.json');
    fs.writeFileSync(contractPath, JSON.stringify(contract, null, 2), 'utf-8');
    const result = spawnSync(process.execPath, [
        path.join(root, 'dist/tools/analyze/assert_pixel_contract.js'),
        '--sequence-dir', sequenceDir,
        '--contract', contractPath,
        '--out-dir', reportDir,
    ], { encoding: 'utf-8' });
    const ok = result.status === 0;
    const status = ok === expectedPass ? 'ok' : 'unexpected';
    return {
        case: name,
        expected: expectedPass ? 'pass' : 'fail',
        actual: ok ? 'pass' : 'fail',
        status,
        dir: caseDir,
    };
}
function main() {
    const args = process.argv.slice(2);
    const requested = argValue(args, '--out-dir');
    const suiteDir = requested
        ? path.resolve(requested)
        : path.join(root, 'out/analysis/pixel-assert-selftest');
    fs.mkdirSync(suiteDir, { recursive: true });
    const results = [];
    // Caso positivo 1: igualdad exacta.
    const equalPass = JSON.parse(JSON.stringify(BASE_CONTRACT));
    equalPass.segments = [{
            name: 'equal-pass',
            frames: [0, -1],
            checks: [{ name: 'equal-full', type: 'equal_region', roi: { x: 0, y: 0, w: 64, h: 64 }, rgbTolerance: 0, maxErrorRatio: 0.0 }],
        }];
    results.push(invokeCase('positive_equal_region', (dir) => {
        solidBitmap(path.join(dir, 'frame_000.png'), [32, 96, 160]);
        copyFrame(path.join(dir, 'frame_000.png'), path.join(dir, 'frame_001.png'));
    }, equalPass, true, suiteDir));
    // Caso negativo 1: igualdad falla por cambio brusco.
    const equalFail = JSON.parse(JSON.stringify(BASE_CONTRACT));
    equalFail.segments = [{
            name: 'equal-fail',
            frames: [0, -1],
            checks: [{ name: 'equal-full', type: 'equal_region', roi: { x: 0, y: 0, w: 64, h: 64 }, rgbTolerance: 0, maxErrorRatio: 0.0 }],
        }];
    results.push(invokeCase('negative_equal_region', (dir) => {
        solidBitmap(path.join(dir, 'frame_000.png'), [0, 0, 255]);
        solidBitmap(path.join(dir, 'frame_001.png'), [255, 0, 0]);
    }, equalFail, false, suiteDir));
    // Caso positivo 2: shifted_region_match con desplazamiento controlado.
    const shiftPass = JSON.parse(JSON.stringify(BASE_CONTRACT));
    shiftPass.segments = [{
            name: 'shift-pass',
            frames: [0, -1],
            checks: [{ name: 'shifted', type: 'shifted_region_match', roi: { x: 8, y: 8, w: 48, h: 48 }, dx: 2, dy: 0, rgbTolerance: 0, maxErrorRatio: 0.0 }],
        }];
    results.push(invokeCase('positive_shifted_region', shiftedRects, shiftPass, true, suiteDir));
    // Caso negativo 2: forbidden_color_ratio detecta negro prohibido.
    const forbiddenFail = JSON.parse(JSON.stringify(BASE_CONTRACT));
    forbiddenFail.globalChecks = [{
            name: 'forbidden-black', type: 'forbidden_color_ratio',
            color: [0, 0, 0], colorTolerance: 0, maxRatio: 0.01,
            roi: { x: 0, y: 0, w: 64, h: 64 },
        }];
    results.push(invokeCase('negative_forbidden_color', forbiddenBlack, forbiddenFail, false, suiteDir));
    const summaryLines = [
        '# Pixel Assert Selftest',
        '',
        '| Case | Expected | Actual | Status |',
        '|---|---:|---:|---:|',
    ];
    for (const item of results) {
        summaryLines.push(`| ${item.case} | ${item.expected} | ${item.actual} | ${item.status} |`);
    }
    const summaryPath = path.join(suiteDir, 'selftest-summary.md');
    const jsonPath = path.join(suiteDir, 'selftest-results.json');
    fs.writeFileSync(summaryPath, summaryLines.join('\n') + '\n', 'utf-8');
    fs.writeFileSync(jsonPath, JSON.stringify(results, null, 2), 'utf-8');
    const unexpected = results.filter((r) => r.status !== 'ok');
    console.log(`OutDir=${suiteDir} Summary=${summaryPath} Results=${jsonPath} ` +
        `Cases=${results.length} Unexpected=${unexpected.length}`);
    if (unexpected.length > 0)
        process.exit(1);
}
main();
