#!/usr/bin/env node
/**
 * Test host del conversor de muestras (`tools/audio/sample-converter.ts`).
 *
 * Valida que `convertSample` escale a la amplitud correcta y rellene al múltiplo
 * de 4, replicando el `SampleConverter.c` del Audio Mixer.
 */
import { convertSample, scaleSample } from './sample-converter.js';
let failures = 0;
function check(cond, msg) {
    if (!cond) {
        console.error(`  [FAIL] ${msg}`);
        failures++;
    }
}
function testScale4Voices() {
    console.log('sample: escala a 4 voces (rango -32..+31)');
    const data = Uint8Array.from([0x80, 0x81, 0x7f, 0x00]); // -128, -127, +127, 0
    const out = scaleSample(data, 4);
    check(out[0] === 0xe0, `-128/4 debe ser -32 (0xe0), fue 0x${out[0].toString(16)}`); // -32
    check(out[1] === 0xe1, `-127/4 trunca a -31 (0xe1), fue 0x${out[1].toString(16)}`); // -31
    check(out[2] === 0x1f, `+127/4 debe ser +31 (0x1f), fue 0x${out[2].toString(16)}`); // 31
    check(out[3] === 0x00, '0/4 debe ser 0');
}
function testScale3VoicesPositive() {
    console.log('sample: 3 voces reduce positivos en 1 antes de dividir');
    const data = Uint8Array.from([0x0c]); // +12
    const out = scaleSample(data, 3);
    // (12 - 1) / 3 = 3
    check(out[0] === 0x03, `(12-1)/3 debe ser 3, fue 0x${out[0].toString(16)}`);
}
function testPadTo4() {
    console.log('sample: relleno a múltiplo de 4');
    const data = Uint8Array.from([1, 2, 3, 4, 5]); // 5 bytes -> 8
    const out = convertSample(data, 1);
    check(out.length === 8, `5 bytes deben rellenarse a 8, fue ${out.length}`);
    check(out[5] === 0 && out[6] === 0 && out[7] === 0, 'el relleno debe ser ceros');
}
testScale4Voices();
testScale3VoicesPositive();
testPadTo4();
if (failures === 0) {
    console.log('OK: conversor de muestras validado (escala + relleno).');
    process.exit(0);
}
console.error(`FAIL: ${failures} comprobacion(es) fallaron`);
process.exit(1);
