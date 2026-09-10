#!/usr/bin/env node
/**
 * Comprobador visual de la demo 055 (copper rainbow).
 *
 * La demo colorea el fondo con un degradado arcoíris (12 franjas de 16 líneas,
 * COLOR00 por franja). Valida:
 *   1. Hay una variedad de colores (degradado, no un color plano).
 *   2. Aparecen varias familias de tono (rojo, verde, azul, amarillo, cian,
 *      magenta) — el arcoíris recorre todo el círculo cromático.
 *
 * Uso: node dist/tools/analyze/verify_copper_rainbow.js --image <png>
 */
import * as fs from 'fs';
import * as path from 'path';
import { argValue, fail } from '../lib/cli.js';
import { readPng } from '../lib/image.js';
function classify(r, g, b) {
    const max = Math.max(r, g, b);
    if (max < 64)
        return null; // demasiado oscuro (fondo/negro)
    const t = 2;
    if (r >= g * t && r >= b * t)
        return 'red';
    if (g >= r * t && g >= b * t)
        return 'green';
    if (b >= r * t && b >= g * t)
        return 'blue';
    if (r >= b * t && g >= b * t)
        return 'yellow';
    if (g >= r * t && b >= r * t)
        return 'cyan';
    if (r >= g * t && b >= g * t)
        return 'magenta';
    return null;
}
function main() {
    const args = process.argv.slice(2);
    const imagePath = argValue(args, '--image');
    if (!imagePath)
        fail('Uso: verify_copper_rainbow.js --image <png>');
    const resolved = path.resolve(imagePath);
    if (!fs.existsSync(resolved))
        fail(`No existe: ${resolved}`);
    const img = readPng(resolved);
    const count = {};
    const tones = new Set();
    for (let y = 0; y < img.height; y++) {
        for (let x = 0; x < img.width; x++) {
            const i = (y * img.width + x) * 4;
            const c = classify(img.data[i], img.data[i + 1], img.data[i + 2]);
            if (c) {
                count[c] = (count[c] ?? 0) + 1;
                tones.add(c);
            }
        }
    }
    // El arcoíris recorre varias familias de tono (mínimo 4 de 6).
    if (tones.size < 4) {
        fail(`FAIL copper: degradado pobre (${tones.size} familias de tono)`);
    }
    const summary = Object.entries(count)
        .sort((a, b) => b[1] - a[1])
        .map(([k, v]) => `${k}=${v}`)
        .join(' ');
    console.log(`OK copper: familias=${tones.size} | ${summary}`);
}
main();
