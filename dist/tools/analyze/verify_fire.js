#!/usr/bin/env node
/**
 * Comprobador visual de la demo 062 (fuego 32 colores + c2p).
 *
 * El fuego produce colores calientes (rojo/naranja/amarillo, r>b) en la parte
 * inferior de la captura, ocupando todo el ancho. Valida:
 *   1. Hay una cantidad minima de pixeles calientes (r>b).
 *   2. Esos pixeles estan en la MITAD INFERIOR (el fuego sube desde abajo).
 *   3. Hay un degradado (varios tonos calientes distintos), no un color plano.
 *
 * Uso: node dist/tools/analyze/verify_fire.js --image <png>
 */
import * as fs from 'fs';
import * as path from 'path';
import { argValue, fail } from '../lib/cli.js';
import { readPng } from '../lib/image.js';
function main() {
    const args = process.argv.slice(2);
    const imagePath = argValue(args, '--image');
    if (!imagePath)
        fail('Uso: verify_fire.js --image <png>');
    const resolved = path.resolve(imagePath);
    if (!fs.existsSync(resolved))
        fail(`No existe: ${resolved}`);
    const img = readPng(resolved);
    const tones = new Set();
    let warm = 0;
    let warmLowerHalf = 0;
    const midY = img.height / 2;
    for (let y = 0; y < img.height; y += 2) {
        for (let x = 0; x < img.width; x += 2) {
            const i = (y * img.width + x) * 4;
            const r = img.data[i], g = img.data[i + 1], b = img.data[i + 2];
            // Caliente: rojo dominante sobre azul (r > b), no negro.
            if (r > b && r > 24) {
                warm++;
                if (y >= midY)
                    warmLowerHalf++;
                tones.add(((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4));
            }
        }
    }
    if (warm < 1000)
        fail(`FAIL fire: muy pocos pixeles calientes (${warm})`);
    if (warmLowerHalf / warm < 0.5) {
        fail(`FAIL fire: el fuego no esta en la mitad inferior (${warmLowerHalf}/${warm})`);
    }
    if (tones.size < 8)
        fail(`FAIL fire: degradado pobre (${tones.size} tonos)`);
    console.log(`OK fire: warm=${warm} lowerHalf=${warmLowerHalf} tones=${tones.size}`);
}
main();
