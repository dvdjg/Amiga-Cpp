#!/usr/bin/env node
/**
 * Comprobador visual de la demo 061 (rotozoom chunky + c2p).
 *
 * La demo llena la pantalla con un rotozoom de paleta ciclica de 16 colores
 * (BPLCON0 de 4 planos, sin HAM). Este validador exige que la captura sea
 * **colorida y con cobertura de pantalla completa**, que es lo que distingue un
 * render correcto de un fallo del c2p (imagen plana, negra o desplazada):
 *
 *   1. Cobertura: la mayoria de los pixeles muestreados no son negros.
 *   2. Riqueza: hay suficientes colores distintos (no un par de tonos planos).
 *   3. Croma: una fraccion apreciable de los pixeles no-negros tiene color real
 *      (no una rampa de grises).
 *
 * Uso: node dist/tools/analyze/verify_c2p_color.js --image <png>
 */
import * as fs from 'fs';
import * as path from 'path';
import { argValue, fail } from '../lib/cli.js';
import { readPng } from '../lib/image.js';
function analyze(imagePath) {
    const image = readPng(imagePath);
    const { width, height, data } = image;
    const colors = new Set();
    const stats = { sampled: 0, nonBlack: 0, chroma: 0, colors: 0 };
    const step = 2;
    for (let y = 0; y < height; y += step) {
        for (let x = 0; x < width; x += step) {
            const i = (y * width + x) * 4;
            const r = data[i], g = data[i + 1], b = data[i + 2];
            stats.sampled++;
            if (r < 8 && g < 8 && b < 8)
                continue;
            stats.nonBlack++;
            const mx = Math.max(r, g, b), mn = Math.min(r, g, b);
            if (mx - mn >= 40)
                stats.chroma++;
            colors.add(((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3));
        }
    }
    stats.colors = colors.size;
    return stats;
}
function main() {
    const args = process.argv.slice(2);
    const imagePath = argValue(args, '--image');
    if (!imagePath) {
        fail('Uso: verify_c2p_color.js --image <png>');
    }
    const resolved = path.resolve(imagePath);
    if (!fs.existsSync(resolved)) {
        fail(`No existe la captura: ${resolved}`);
    }
    const s = analyze(resolved);
    if (s.nonBlack < s.sampled * 0.55) {
        fail(`FAIL c2p: solo ${s.nonBlack}/${s.sampled} pixeles no negros (no llena la pantalla)`);
    }
    if (s.colors < 12) {
        fail(`FAIL c2p: solo ${s.colors} colores distintos (esperaba >=12, render plano)`);
    }
    if (s.chroma < s.nonBlack * 0.3) {
        fail(`FAIL c2p: solo ${s.chroma}/${s.nonBlack} pixeles con color real (¿rampa de grises?)`);
    }
    console.log(`OK c2p: colores=${s.colors} croma=${s.chroma}/${s.nonBlack} cobertura=${s.nonBlack}/${s.sampled}`);
}
main();
