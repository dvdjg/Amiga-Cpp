#!/usr/bin/env node

// Benchmark determinista del ejecutable host: mide tiempo de proceso y conserva la salida para
// comparar codecs/chunks/rutas sin afirmar memoria RSS que varía según el sistema operativo.
import { performance } from 'node:perf_hooks';
import { spawnSync } from 'node:child_process';
import { existsSync } from 'node:fs';

const [executable, input, output = 'out/tmp/audio-compressor/bench.auzx'] = process.argv.slice(2);
if (!executable || !input) {
	console.error('Uso: node tools/bench-audio-compressor.mjs <ejecutable> <entrada> [salida]');
	process.exit(2);
}
const start = performance.now();
const args = [input, '--mode', 'sample', '--codec', 'none', '--chunk', '4096', '--force', '--out', output];
const timed = process.platform !== 'win32' && existsSync('/usr/bin/time');
const result = spawnSync(timed ? '/usr/bin/time' : executable, timed ? ['-f', '%M', executable, ...args] : args, {
	encoding: 'utf8',
});
const elapsedMs = performance.now() - start;
const stderrLines = (result.stderr ?? '').trimEnd().split(/\r?\n/);
const maxRss = timed && /^\d+$/.test(stderrLines.at(-1) ?? '') ? Number(stderrLines.pop()) : null;
const report = {
	input,
	output,
	elapsed_ms: Number(elapsedMs.toFixed(3)),
	status: result.status,
	signal: result.signal,
	max_rss_kb: maxRss,
	memory_note: timed ? 'GNU time RSS' : 'RSS no disponible de forma portable en Windows; use el monitor nativo del entorno',
};
process.stdout.write(`${JSON.stringify(report)}\n`);
if (result.status !== 0) {
	process.stderr.write(result.stderr ?? 'audio-compressor failed\n');
	process.exit(result.status ?? 1);
}
