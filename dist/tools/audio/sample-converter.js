#!/usr/bin/env node
/**
 * Conversor de muestras para el Audio Mixer 3.7 (Photon).
 *
 * Preprocesa una muestra 8-bit con signo para que pueda mezclarse sin overflow:
 * divide cada byte por el número de voces (mixer_sw_channels) y rellena la
 * longitud al múltiplo de 4 bytes. Replica el `SampleConverter.c` del proyecto
 * original (división entera truncada a cero; en 3 voces los positivos se
 * reducen en 1 antes de dividir).
 *
 * Uso: node dist/tools/audio/sample-converter.js <voices> <entrada.raw> <salida.raw>
 *
 * La función pura `convertSample` es la que usa el test host.
 */
import * as fs from 'fs';
import { pathToFileURL } from 'url';
import { fail } from '../lib/cli.js';
/** Divide (con signo) cada byte por `voices` (truncando a cero, como C). */
export function scaleSample(data, voices) {
    const out = new Uint8Array(data.length);
    for (let i = 0; i < data.length; i++) {
        let v = data[i] > 127 ? data[i] - 256 : data[i]; // 8-bit con signo
        if (voices === 3 && v > 0) {
            v -= 1;
        }
        v = (v / voices) | 0; // truncado a cero
        out[i] = v & 0xff;
    }
    return out;
}
/** Rellena con ceros hasta el múltiplo de 4 bytes. */
export function padToMultiple(data, multiple = 4) {
    const padded = (data.length + (multiple - 1)) & ~(multiple - 1);
    if (padded === data.length) {
        return data;
    }
    const out = new Uint8Array(padded);
    out.set(data);
    return out;
}
/** Convierte y rellena una muestra para el mixer. */
export function convertSample(data, voices) {
    return padToMultiple(scaleSample(data, voices), 4);
}
function main() {
    const args = process.argv.slice(2);
    const voicesStr = args[0];
    const input = args[1];
    const output = args[2];
    if (!voicesStr || !input || !output) {
        console.error('Uso: sample-converter.js <voices 1..4> <entrada.raw> <salida.raw>');
        process.exit(2);
    }
    const voices = parseInt(voicesStr, 10);
    if (voices < 1 || voices > 4) {
        fail('voices debe estar entre 1 y 4');
    }
    const data = fs.readFileSync(input);
    const out = convertSample(new Uint8Array(data), voices);
    fs.writeFileSync(output, out);
    console.log(`OK sample: ${data.length} -> ${out.length} bytes (voices=${voices})`);
}
// Solo ejecuta el CLI al invocar el script directamente (no al importarlo).
if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
    main();
}
