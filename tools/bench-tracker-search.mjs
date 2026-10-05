#!/usr/bin/env node

// Busca configuraciones tracker reproducibles sin afirmar que una separacion espectral sea instrumental.
import { spawnSync } from 'node:child_process';
import path from 'node:path';

const [input, ...options] = process.argv.slice(2);
if (!input) {
	console.error('Uso: node tools/bench-tracker-search.mjs <entrada.wav|ogg> [--rows 512,768,1024,1323,1536,2048] [--prototypes 3,8] [--residual-weight N]');
	process.exit(2);
}

function option(name, fallback) {
	const index = options.indexOf(name);
	return index >= 0 && options[index + 1] ? options[index + 1] : fallback;
}

const rows = option('--rows', '512,768,1024,1323,1536,2048').split(',').map(Number).filter((value) => Number.isFinite(value) && value > 0);
const prototypes = option('--prototypes', '3,8').split(',').map(Number).filter((value) => Number.isFinite(value) && value > 0);
const residualWeight = Number(option('--residual-weight', '200000'));
const executable = path.resolve('out/tmp/audio-compressor', process.platform === 'win32' ? 'audio-compressor.exe' : 'audio-compressor');
const results = [];

for (const prototypeCount of prototypes) {
	for (const rowSamples of rows) {
		const output = path.resolve('out/tmp/audio-compressor', `tracker-search-${prototypeCount}-${rowSamples}.acp1`);
		const exportDir = path.resolve('out/tmp/audio-compressor', `tracker-search-${prototypeCount}-${rowSamples}`);
		const result = spawnSync(executable, [path.resolve(input), '--spectral-separate', '--spectral-max-prototypes', String(prototypeCount), '--tracker-row-samples', String(rowSamples), '--spectral-export-dir', exportDir, '--force', '--out', output], { encoding: 'utf8', maxBuffer: 1024 * 1024 });
		if (result.status !== 0) {
			console.error(result.stderr || result.stdout || `Falló la configuración ${prototypeCount}/${rowSamples}`);
			process.exit(result.status ?? 1);
		}
		const line = `${result.stdout}\n${result.stderr}`.match(/spectral-separation=ok[^\n]*/)?.[0] ?? '';
		const pcm = `${result.stdout}\n${result.stderr}`.match(/MSE_PCM=([-0-9.]+) SNR_PCM=([-0-9.]+)/);
		const read = (name) => Number(line.match(new RegExp(`${name}=([-0-9.]+)`))?.[1] ?? NaN);
		const entry = {
			prototypes: prototypeCount,
			row_samples: rowSamples,
			tracker_bytes: read('bytes_tracker'),
			events: read('eventos'),
			patterns: read('patrones'),
			pattern_event_savings: read('ahorro_eventos'),
			residual: read('residual'),
			pcm_mse: pcm ? Number(pcm[1]) : null,
			pcm_snr_db: pcm ? Number(pcm[2]) : null,
		};
		entry.score = entry.tracker_bytes + entry.residual * residualWeight;
		results.push(entry);
	}
}

results.sort((left, right) => left.score - right.score);
const pareto = results.filter((candidate, index) => !results.some((other, otherIndex) => otherIndex !== index && other.tracker_bytes <= candidate.tracker_bytes && other.residual <= candidate.residual && (other.tracker_bytes < candidate.tracker_bytes || other.residual < candidate.residual)));
console.log(JSON.stringify({ input, residual_weight: residualWeight, best: results[0], pareto: pareto.sort((left, right) => left.tracker_bytes - right.tracker_bytes), candidates: results }, null, 2));
