#!/usr/bin/env node
/**
 * Generación y análisis de ondas de audio (PCM 8-bit con signo), para depurar
 * el pipeline de sonido del engine ANTES de llevarlo a Paula.
 *
 * - `generateWave`: genera seno/cuadrada/triangular/sierra 8-bit con signo.
 * - `analyzeWave`: amplitud (min/max), DC, RMS y frecuencia dominante (por
 *   cruces por cero). Sirve para verificar que una muestra es lo que se pretende.
 * - `toWav`: envuelve una muestra en un WAV (8-bit sin signo) para escucharla o
 *   inspeccionarla en una herramienta externa.
 *
 * Todo es puro y host-testable (test en `test-wave.ts`).
 */
/** Genera `durationSeconds` segundos de una onda 8-bit con signo (±amplitude). */
export function generateWave(kind, frequency, sampleRate, durationSeconds, amplitude = 127) {
    const n = Math.round(sampleRate * durationSeconds);
    const out = new Int8Array(n);
    const phaseStep = frequency / sampleRate;
    for (let i = 0; i < n; i++) {
        const phase = i * phaseStep; // ciclos (0..1 = 1 ciclo)
        const p = phase - Math.floor(phase); // fase normalizada 0..1
        let v = 0;
        switch (kind) {
            case 'sine':
                v = Math.sin(2 * Math.PI * p);
                break;
            case 'square':
                v = p < 0.5 ? 1 : -1;
                break;
            case 'triangle':
                v = p < 0.5 ? 4 * p - 1 : 3 - 4 * p;
                break;
            case 'saw':
                v = 2 * p - 1;
                break;
        }
        out[i] = Math.round(v * amplitude);
    }
    return out;
}
/** Analiza una muestra: amplitud, DC, RMS y frecuencia dominante (cruces por cero). */
export function analyzeWave(data, sampleRate) {
    const n = data.length;
    let min = 0;
    let max = 0;
    let sum = 0;
    let sumSq = 0;
    let crossings = 0;
    let prev = toSigned(data[0]);
    for (let i = 0; i < n; i++) {
        const v = toSigned(data[i]);
        if (v < min)
            min = v;
        if (v > max)
            max = v;
        sum += v;
        sumSq += v * v;
        if (i > 0 && ((prev < 0 && v >= 0) || (prev > 0 && v <= 0))) {
            crossings++;
        }
        prev = v;
    }
    const dc = sum / n;
    const rms = Math.sqrt(sumSq / n);
    const dominantHz = (crossings / 2) * (sampleRate / n);
    return { count: n, min, max, dc, rms, zeroCrossings: crossings, dominantHz };
}
/** Envuelve una muestra 8-bit con signo en un WAV 8-bit sin signo (mono). */
export function toWav(data, sampleRate) {
    const dataSize = data.length;
    const buf = Buffer.alloc(44 + dataSize);
    buf.write('RIFF', 0, 'ascii');
    buf.writeUInt32LE(36 + dataSize, 4);
    buf.write('WAVE', 8, 'ascii');
    buf.write('fmt ', 12, 'ascii');
    buf.writeUInt32LE(16, 16); // tamaño del chunk fmt
    buf.writeUInt16LE(1, 20); // PCM
    buf.writeUInt16LE(1, 22); // mono
    buf.writeUInt32LE(sampleRate, 24);
    buf.writeUInt32LE(sampleRate, 28); // byte rate (1 byte/muestra)
    buf.writeUInt16LE(1, 32); // block align
    buf.writeUInt16LE(8, 34); // bits por muestra
    buf.write('data', 36, 'ascii');
    buf.writeUInt32LE(dataSize, 40);
    for (let i = 0; i < dataSize; i++) {
        buf[44 + i] = (data[i] + 128) & 0xff; // con signo -> sin signo
    }
    return buf;
}
function toSigned(b) {
    return b > 127 ? b - 256 : b;
}
