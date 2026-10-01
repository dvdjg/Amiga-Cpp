#!/usr/bin/env node

// Benchmark determinista del ejecutable host: mide tiempo de proceso y conserva la salida para
// comparar codecs/chunks/rutas sin afirmar memoria RSS que varía según el sistema operativo.
import { performance } from 'node:perf_hooks';
import { spawnSync } from 'node:child_process';

const [executable, input, output = 'out/tmp/audio-compressor/bench.auzx'] = process.argv.slice(2);
if (!executable || !input) {
	console.error('Uso: node tools/bench-audio-compressor.mjs <ejecutable> <entrada> [salida]');
	process.exit(2);
}
const start = performance.now();
const result = spawnSync(executable, [input, '--mode', 'sample', '--codec', 'none', '--chunk', '4096', '--force', '--out', output], {
	encoding: 'utf8',
});
const elapsedMs = performance.now() - start;
const report = {
	input,
	output,
	elapsed_ms: Number(elapsedMs.toFixed(3)),
	status: result.status,
	signal: result.signal,
};
process.stdout.write(`${JSON.stringify(report)}\n`);
if (result.status !== 0) {
	process.stderr.write(result.stderr ?? 'audio-compressor failed\n');
	process.exit(result.status ?? 1);
}
