#!/usr/bin/env node
// Cooker host de mapas Tiled: compone el bake, cuantiza perfiles Amiga, corta
// tilebanks y emite un informe de propuestas. La decisión final (perfil y paleta)
// es del usuario/IA; `--select` promueve una opción a `selected/`.
//
// Uso:
//   node tools/tiled/cook-map.mjs <mapa.tmx> --plan cook-plan.json --out out/assets/tiled/<mapa>
//        [--profiles auto|16c,32c,ehb] [--dither checker|none] [--select <perfil>]
//        [--palette paleta.json --planes N]   (perfil `custom`)
//
// Perfiles automáticos: 16c (4 planos), 32c (5 planos), ehb (6 planos EHB).
// El perfil EHB se propone cuando hay capas de sombra o >32 colores únicos; el
// usuario puede imponer otro con --select o una paleta con --palette.

import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { PNG } from 'pngjs';
import { parseTmxFile } from './parse-tmx.mjs';
import { validateCookPlan } from './validate-cook-plan.mjs';
import { composeMap } from './compose-map.mjs';
import { packMapBundle, bundleHeader } from './map_bundle.mjs';
import { cookAnimations } from './cook-animations.mjs';
import { cookObjects } from './cook-objects.mjs';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const QUANTIZE = path.join(ROOT, 'tools/ehb/quantize-ehb.mjs');
const SLICE = path.join(ROOT, 'tools/ehb/slice-tiles.mjs');
const EMIT = path.join(ROOT, 'tools/ehb/emit-xlimited-bank.mjs');
const PROFILES = {
	'16c': { force: '4', planes: 4, colors: 16 },
	'32c': { force: '5', planes: 5, colors: 32 },
	ehb: { force: 'ehb', planes: 6, colors: 64 },
};

const argValue = (args, name) => {
	const at = args.indexOf(name);
	return at >= 0 ? args[at + 1] : null;
};

function runNode(script, args) {
	const result = spawnSync(process.execPath, [script, ...args], { cwd: ROOT, encoding: 'utf8' });
	if (result.status !== 0) {
		throw new Error(`${path.basename(script)} falló (${result.status})\n${result.stdout ?? ''}\n${result.stderr ?? ''}`);
	}
	return result.stdout ?? '';
}

function uniqueColors(pngPath) {
	const png = PNG.sync.read(fs.readFileSync(pngPath));
	const seen = new Set();
	for (let i = 0; i < png.width * png.height; ++i) {
		const o = i * 4;
		if (png.data[o + 3] < 128) continue;
		seen.add((png.data[o] << 16) | (png.data[o + 1] << 8) | png.data[o + 2]);
	}
	return seen.size;
}

function psnrAgainst(sourcePath, reconstructPath) {
	if (!fs.existsSync(reconstructPath)) return null;
	const a = PNG.sync.read(fs.readFileSync(sourcePath));
	const b = PNG.sync.read(fs.readFileSync(reconstructPath));
	if (a.width !== b.width || a.height !== b.height) return null;
	let se = 0;
	let n = 0;
	for (let i = 0; i < a.width * a.height; ++i) {
		const o = i * 4;
		for (let c = 0; c < 3; ++c) {
			const d = a.data[o + c] - b.data[o + c];
			se += d * d;
			++n;
		}
	}
	const mse = se / Math.max(1, n);
	return mse <= 0 ? 99 : 10 * Math.log10((255 * 255) / mse);
}

function flattenNames(layers, out = []) {
	for (const layer of layers) {
		out.push(layer.name ?? '');
		if (layer.kind === 'group') flattenNames(layer.layers ?? [], out);
	}
	return out;
}

function profileReport(name, planes, dir, quantOut, sourcePng) {
	const tilesPath = path.join(dir, 'tiles.json');
	const rawPath = path.join(dir, 'tilebank.raw.bin');
	const xlPath = path.join(dir, 'tilebank.xlimited.bin');
	const reconPath = path.join(dir, 'reconstruct.png');
	const tiles = JSON.parse(fs.readFileSync(tilesPath, 'utf8'));
	const psnr = psnrAgainst(sourcePng, reconPath);
	const bytes = (file) => (fs.existsSync(file) ? fs.statSync(file).size : 0);
	const displayBytes = (320 / 8) * 256 * planes;
	return {
		name,
		planes,
		colors: 1 << planes,
		uniqueTiles: tiles.stats?.unique ?? 0,
		cols: tiles.cols,
		rows: tiles.rows,
		rawBytes: bytes(rawPath),
		bankBytes: bytes(xlPath),
		displayBytes,
		paletteBytes: 2 * (1 << planes),
		chipEstimate: bytes(xlPath) + displayBytes + 2 * (1 << planes) + 8192,
		psnr: psnr == null ? null : Number(psnr.toFixed(2)),
		paths: {
			dir: path.relative(ROOT, dir),
			palette: path.relative(ROOT, path.join(dir, '..', 'quant', 'palette.json')),
			tilebankIndexed: path.relative(ROOT, path.join(dir, 'tilebank_indexed.h')),
			tilebankXlimited: path.relative(ROOT, xlPath),
			tiles: path.relative(ROOT, tilesPath),
			reconstruct: path.relative(ROOT, reconPath),
		},
		quantOutput: quantOut,
	};
}

function recommend(profiles, unique, hasShadow) {
	const byName = new Map(profiles.map((p) => [p.name, p]));
	const preferred = hasShadow || unique > 32 ? 'ehb' : unique <= 16 ? '16c' : '32c';
	return byName.has(preferred) ? preferred : profiles.slice().sort((a, b) => (b.psnr ?? 0) - (a.psnr ?? 0))[0]?.name ?? null;
}

function writeSelected(outDir, profile, ir, plan) {
	const selectedDir = path.join(outDir, 'selected');
	fs.mkdirSync(selectedDir, { recursive: true });
	const profileDir = path.join(outDir, 'profiles', profile.name);
	const tiles = JSON.parse(fs.readFileSync(path.join(profileDir, 'tiles.json'), 'utf8'));
	fs.copyFileSync(path.join(profileDir, 'tilebank_indexed.h'), path.join(selectedDir, 'tilebank_indexed.h'));
	fs.copyFileSync(path.join(profileDir, 'tilebank.xlimited.bin'), path.join(selectedDir, 'tilebank.xlimited.bin'));
	const header = [
		'// Mapa Tiled cocinado: perfil seleccionado. Generado por tools/tiled/cook-map.mjs.',
		`// Perfil ${profile.name}: ${profile.planes} planos, ${profile.uniqueTiles} tiles, ${profile.cols}x${profile.rows} celdas.`,
		'#pragma once',
		`#define TILED_MAP_W ${tiles.cols}u`,
		`#define TILED_MAP_H ${tiles.rows}u`,
		`#define TILED_MAP_TILES ${tiles.stats.unique}u`,
		`#define TILED_MAP_PLANES ${profile.planes}u`,
		`#define TILED_MAP_TILE_W ${tiles.tile}u`,
		`#define TILED_MAP_TILE_H ${tiles.tile}u`,
		`#define TILED_MAP_STRIDE ${tiles.tile * tiles.tile}u`,
		'',
		'#include "tilebank_indexed.h"',
		'',
	].join('\n');
	fs.writeFileSync(path.join(selectedDir, 'tiled_map.h'), header, 'utf8');
	// MapBundle v1: banco externo + una capa baked + paleta. La demo lo lee con
	// eng::map::MapAssetView; el header C sigue disponible para el camino r?pido.
	const bundle = packMapBundle({
		tileWidth: tiles.tile,
		tileHeight: tiles.tile,
		planes: profile.planes,
		tileCount: tiles.stats.unique,
		bankBytes: fs.statSync(path.join(profileDir, 'tilebank.xlimited.bin')).size,
		palette: tiles.palette,
		layers: [{
			id: 0,
			mode: 0,
			width: tiles.cols,
			height: tiles.rows,
			originX: 0,
			originY: 0,
			wrapX: tiles.cols,
			wrapY: tiles.rows,
			emptyTile: 0xffff,
			cells: tiles.map,
		}],
	});
	fs.writeFileSync(path.join(selectedDir, 'map.bundle'), bundle);
	fs.writeFileSync(path.join(selectedDir, 'map_bundle.h'), bundleHeader('kMapBundle', bundle), 'utf8');
	const animations = cookAnimations({
		ir,
		plan,
		paletteFile: path.join(profileDir, 'quant', 'palette.json'),
		planes: profile.planes,
		outDir: selectedDir,
	});
	const objects = cookObjects({
		ir,
		plan,
		paletteFile: path.join(profileDir, 'quant', 'palette.json'),
		planes: profile.planes,
		outDir: selectedDir,
	});
	const selected = {
		profile: profile.name,
		planes: profile.planes,
		uniqueTiles: profile.uniqueTiles,
		cols: profile.cols,
		rows: profile.rows,
		psnr: profile.psnr,
		header: path.relative(ROOT, path.join(selectedDir, 'tiled_map.h')),
		tilebankIndexed: path.relative(ROOT, path.join(selectedDir, 'tilebank_indexed.h')),
		tilebankXlimited: path.relative(ROOT, path.join(selectedDir, 'tilebank.xlimited.bin')),
		bundle: path.relative(ROOT, path.join(selectedDir, 'map.bundle')),
		bundleHeader: path.relative(ROOT, path.join(selectedDir, 'map_bundle.h')),
		animations: animations == null ? null : {
			header: path.relative(ROOT, path.join(selectedDir, 'animations.h')),
			sheet: path.relative(ROOT, path.join(selectedDir, 'anim_sheet.bin')),
			masks: path.relative(ROOT, path.join(selectedDir, 'anim_masks.bin')),
			report: path.relative(ROOT, path.join(selectedDir, 'animations.json')),
		},
		objects: objects == null ? null : {
			header: path.relative(ROOT, path.join(selectedDir, 'objects.h')),
			sheet: path.relative(ROOT, path.join(selectedDir, 'object_sheet.bin')),
			masks: path.relative(ROOT, path.join(selectedDir, 'object_masks.bin')),
			report: path.relative(ROOT, path.join(selectedDir, 'objects.json')),
		},
	};
	fs.writeFileSync(path.join(outDir, 'selected.json'), JSON.stringify(selected, null, 2) + '\n', 'utf8');
	return selected;
}

function main() {
	const args = process.argv.slice(2);
	let file = null;
	let planFile = null;
	let outDir = null;
	let paletteFile = null;
	let planes = null;
	let select = null;
	let dither = 'checker';
	for (let k = 0; k < args.length; ++k) {
		const arg = args[k];
		if (arg === '--plan') { planFile = args[++k] ?? null; continue; }
		if (arg === '--out') { outDir = args[++k] ?? null; continue; }
		if (arg === '--palette') { paletteFile = args[++k] ?? null; continue; }
		if (arg === '--planes') { planes = Number(args[++k] ?? '5'); continue; }
		if (arg === '--select') { select = args[++k] ?? null; continue; }
		if (arg === '--dither') { dither = args[++k] ?? 'checker'; continue; }
		if (arg.startsWith('--')) continue;
		if (file == null) file = arg;
	}
	if (file == null) {
		console.error('Uso: node tools/tiled/cook-map.mjs <mapa.tmx> [--plan cook-plan.json] [--out dir] [--profiles 16c,32c,ehb] [--dither checker] [--select perfil]');
		process.exit(2);
	}
	if (dither !== 'checker' && dither !== 'none') {
		console.error('[cook] --dither solo acepta checker|none');
		process.exit(2);
	}
	const source = path.resolve(file);
	const out = path.resolve(outDir ?? `out/assets/tiled/${path.basename(source, path.extname(source))}`);
	fs.mkdirSync(out, { recursive: true });
	const ir = parseTmxFile(source);
	const plan = planFile != null ? JSON.parse(fs.readFileSync(path.resolve(planFile), 'utf8')) : null;
	const planReport = validateCookPlan(ir, plan ?? {});
	if (planReport.warnings.length > 0) {
		for (const warning of planReport.warnings) console.warn(`[cook] aviso de plan: ${warning}`);
	}
	if (!planReport.ok) {
		for (const error of planReport.errors) console.error(`[cook] error de plan: ${error}`);
		process.exit(1);
	}
	const composed = composeMap(ir, plan);
	const composedPath = path.join(out, 'composed.png');
	fs.writeFileSync(composedPath, PNG.sync.write(composed.png));
	// La paleta no puede salir solo del bake: las capas vivas (agua, flores,
	// fuego) y los objetos runtime no están en `composed.png`. Se compone una
	// referencia que incluye todas las capas y objetos con su primer frame; es
	// una entrada más del cooker, no un caso especial de este mapa.
	const paletteSource = composeMap(ir, {}, { palette: true });
	const paletteSourcePath = path.join(out, 'palette-source.png');
	fs.writeFileSync(paletteSourcePath, PNG.sync.write(paletteSource.png));
	fs.writeFileSync(path.join(out, 'compose.json'), JSON.stringify({
		source: ir.source,
		bounds: composed.bounds,
		stats: composed.stats,
		paletteSource: path.relative(ROOT, paletteSourcePath),
		plan: planReport,
		warnings: composed.warnings,
		errors: composed.errors,
	}, null, 2) + '\n', 'utf8');
	if (composed.errors.length > 0) {
		for (const error of composed.errors) console.error(`[cook] ${error}`);
		process.exit(1);
	}
	const unique = uniqueColors(composedPath);
	const names = flattenNames(ir.layers);
	const hasShadow = names.some((name) => /shadow|sombra/i.test(name));
	const selectedProfiles = [];
	if (paletteFile != null) {
		const palettePath = path.resolve(paletteFile);
		const palette = JSON.parse(fs.readFileSync(palettePath, 'utf8'));
		const bases = Array.isArray(palette) ? palette : palette.bases;
		const inferred = Array.isArray(palette) ? (bases.length <= 16 ? 4 : bases.length <= 32 ? 5 : 6) : (palette.planes ?? (bases.length <= 16 ? 4 : bases.length <= 32 ? 5 : 6));
		const p = planes ?? inferred;
		const dir = path.join(out, 'profiles', 'custom');
		fs.mkdirSync(path.join(dir, 'quant'), { recursive: true });
		fs.copyFileSync(palettePath, path.join(dir, 'quant', 'palette.json'));
		const sliceOut = runNode(SLICE, [composedPath, '--palette', path.join(dir, 'quant', 'palette.json'), '--out', dir, '--dither', dither]);
		runNode(EMIT, ['--planes', String(p), '--in', path.join(dir, 'tilebank.raw.bin'), '--out-bin', path.join(dir, 'tilebank.xlimited.bin'), '--out-h', path.join(dir, 'tilebank.xlimited.h')]);
		selectedProfiles.push(profileReport('custom', p, dir, sliceOut, composedPath));
	} else {
		const requested = (argValue(args, '--profiles') ?? '16c,32c,ehb').split(',').map((s) => s.trim()).filter(Boolean);
		const profileNames = requested.includes('auto') ? ['16c', '32c', 'ehb'] : requested;
		for (const name of profileNames) {
			const profile = PROFILES[name];
			if (profile == null) {
				console.error(`[cook] perfil desconocido "${name}" (usa 16c,32c,ehb o --palette)`);
				process.exit(2);
			}
			const dir = path.join(out, 'profiles', name);
			const quantDir = path.join(dir, 'quant');
			fs.mkdirSync(quantDir, { recursive: true });
			const quantOut = runNode(QUANTIZE, [paletteSourcePath, '--force', profile.force, '--out', quantDir]);
			const sliceOut = runNode(SLICE, [composedPath, '--palette', path.join(quantDir, 'palette.json'), '--out', dir, '--dither', dither]);
			runNode(EMIT, ['--planes', String(profile.planes), '--in', path.join(dir, 'tilebank.raw.bin'), '--out-bin', path.join(dir, 'tilebank.xlimited.bin'), '--out-h', path.join(dir, 'tilebank.xlimited.h')]);
			selectedProfiles.push(profileReport(name, profile.planes, dir, quantOut + sliceOut, composedPath));
		}
	}
	const recommended = recommend(selectedProfiles, unique, hasShadow);
	const proposal = {
		map: path.relative(ROOT, source),
		composed: path.relative(ROOT, composedPath),
		paletteSource: path.relative(ROOT, paletteSourcePath),
		bounds: composed.bounds,
		uniqueColors: unique,
		hasShadowLayer: hasShadow,
		dither,
		profiles: selectedProfiles,
		recommended,
	};
	fs.writeFileSync(path.join(out, 'proposal.json'), JSON.stringify(proposal, null, 2) + '\n', 'utf8');
	const lines = [
		`# Cook Tiled: ${path.basename(source)}`,
		'',
		`Lienzo bake: ${composed.bounds.width}x${composed.bounds.height}; colores únicos: ${unique}; capa de sombra: ${hasShadow ? 'sí' : 'no'}; dither: ${dither}.`,
		'',
		'| Perfil | Planos | Colores | Tiles únicos | Banco interleaved | Raw | PSNR | Chip estimado |',
		'|---|---:|---:|---:|---:|---:|---:|---:|',
		...selectedProfiles.map((p) => `| ${p.name} | ${p.planes} | ${p.colors} | ${p.uniqueTiles} | ${(p.bankBytes / 1024).toFixed(0)} KB | ${(p.rawBytes / 1024).toFixed(0)} KB | ${p.psnr ?? '-'} | ${(p.chipEstimate / 1024).toFixed(0)} KB |`),
		'',
		`**Recomendación automática:** \`${recommended ?? 'ninguna'}\`. La IA puede proponer alternativas con estos datos; la decisión final (perfil/paleta) es del usuario.`,
		'',
		`Para promover una opción: \`node tools/tiled/cook-map.mjs "${path.relative(ROOT, source)}" --out "${path.relative(ROOT, out)}" --select ${recommended ?? 'ehb'}\`.`,
		'',
		'La opción EHB es la recomendada cuando hay sombras o más de 32 colores: el sexto plano genera los half-brights y evita gastar una paleta plana de 64 colores. `--palette` impone la paleta final y `--planes` su profundidad.',
	];
	fs.writeFileSync(path.join(out, 'proposal.md'), lines.join('\n') + '\n', 'utf8');
	let selected = null;
	const chosen = select ?? (paletteFile != null ? 'custom' : null);
	if (chosen != null) {
		const profile = selectedProfiles.find((p) => p.name === chosen);
		if (profile == null) { console.error(`[cook] no existe el perfil "${chosen}"`); process.exit(2); }
		selected = writeSelected(out, profile, ir, plan);
	}
	console.log(`[cook] ${path.basename(source)} -> ${path.relative(ROOT, out)}`);
	console.log(`[cook] ${composed.bounds.width}x${composed.bounds.height}, uniqueColors=${unique}, shadow=${hasShadow}, dither=${dither}`);
	for (const p of selectedProfiles) console.log(`[cook] ${p.name}: planes=${p.planes} tiles=${p.uniqueTiles} bank=${(p.bankBytes / 1024).toFixed(0)}KB raw=${(p.rawBytes / 1024).toFixed(0)}KB psnr=${p.psnr ?? '-'}`);
	console.log(`[cook] recomendado=${recommended ?? '-'}${selected != null ? ` seleccionado=${selected.profile}` : ''}`);
}

if (process.argv[1] != null && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
	main();
}
