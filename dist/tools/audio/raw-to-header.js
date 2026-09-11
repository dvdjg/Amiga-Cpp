#!/usr/bin/env node
/**
 * Convierte un `.raw` (PCM 8-bit) en una cabecera C++ con el array de bytes,
 * para incrustar una muestra generada en host dentro de una demo Amiga (misma
 * onda, byte a byte). Es el puente "host -> Amiga" del pipeline de depuración.
 *
 * Uso: node dist/tools/audio/raw-to-header.js <in.raw> <out.h> <nombre>
 */
import * as fs from 'fs';
const args = process.argv.slice(2);
const input = args[0];
const output = args[1];
const name = args[2] ?? 'sample';
if (!input || !output) {
    console.error('Uso: raw-to-header.js <in.raw> <out.h> <nombre>');
    process.exit(2);
}
const data = fs.readFileSync(input);
const lines = [];
lines.push(`// Generado a partir de ${input} (${data.length} bytes, PCM 8-bit con signo).`);
lines.push(`#pragma once`);
lines.push(`constexpr eng::u8 ${name}[${data.length}] = {`);
for (let i = 0; i < data.length; i += 16) {
    const chunk = Array.from(data.subarray(i, i + 16))
        .map((b) => `0x${b.toString(16).padStart(2, '0')}u`)
        .join(', ');
    lines.push(`\t${chunk},`);
}
lines.push('};');
lines.push(`constexpr eng::u32 ${name}_len = ${data.length};`);
fs.writeFileSync(output, lines.join('\n') + '\n');
console.log(`OK: ${data.length} bytes -> ${output} (${name})`);
