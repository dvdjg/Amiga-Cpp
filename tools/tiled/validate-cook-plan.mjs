#!/usr/bin/env node
// Valida el contrato de capas de un cook-plan antes de cocinar.
//
// Regla de oro: la base es un PREFIJO opaco. Si una capa deja de estar cocida,
// todas las capas que se dibujan por encima de ella deben seguir siendo vivas
// (overlay/animated/occluder), no pueden fundirse en la base que queda debajo.

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { parseTmxFile } from './parse-tmx.mjs';

const MODES = new Set([
	'baked',
	'base',
	'overlay',
	'animated',
	'occluder',
	'parallax',
	'skip',
	'collision',
	'collisiononly',
	'actors',
	'bake',
]);

const LIVE_MODES = new Set(['overlay', 'animated', 'occluder']);

const layerName = (layer) => layer.name || `#${layer.id}`;

function flatten(layers, out = []) {
	for (const layer of layers ?? []) {
		if (layer.visible === false) continue;
		if (layer.kind === 'group') {
			flatten(layer.layers, out);
			continue;
		}
		out.push(layer);
	}
	return out;
}

function modeFor(plan, layer) {
	const name = layerName(layer);
	const explicit = plan?.render?.modes?.[name];
	if (explicit != null) return explicit;
	const table = layer.kind === 'object' ? plan?.objects : plan?.layers;
	return table?.[name] ?? (layer.kind === 'object' ? 'bake' : 'baked');
}

function isBaked(mode) {
	return mode === 'baked' || mode === 'bake' || mode === 'base';
}

function isLive(mode) {
	return LIVE_MODES.has(mode);
}

/**
 * @param {object} ir IR de parse-tmx
 * @param {object} plan cook-plan
 * @returns {{ok:boolean, errors:string[], warnings:string[], baked_through:string|null, modes:object[]}}
 */
export function validateCookPlan(ir, plan = {}) {
	const errors = [];
	const warnings = [];
	const ordered = flatten(ir.layers);
	const modes = ordered.map((layer) => ({
		layer,
		name: layerName(layer),
		kind: layer.kind,
		mode: modeFor(plan, layer),
	}));
	for (const item of modes) {
		if (!MODES.has(item.mode)) {
			errors.push(`${item.name}: modo de capa desconocido "${item.mode}"`);
		}
	}

	const explicit = plan?.render?.baked_through ?? null;
	let bakedThrough = explicit;
	if (bakedThrough != null) {
		const at = modes.findIndex((item) => item.name === bakedThrough);
		if (at < 0) {
			errors.push(`render.baked_through: no existe la capa "${bakedThrough}"`);
		} else {
			for (let i = 0; i <= at; ++i) {
				if (!isBaked(modes[i].mode)) {
					errors.push(
						`${modes[i].name}: está por debajo o es baked_through y no está cocida (${modes[i].mode})`,
					);
				}
			}
			for (let i = at + 1; i < modes.length; ++i) {
				if (isBaked(modes[i].mode)) {
					errors.push(
						`${modes[i].name}: está por encima de baked_through y no puede estar cocida (${modes[i].mode})`,
					);
				}
			}
		}
	} else {
		let firstLive = modes.findIndex((item) => isLive(item.mode));
		if (firstLive < 0) {
			bakedThrough = modes.length > 0 ? modes[modes.length - 1].name : null;
		} else {
			bakedThrough = firstLive > 0 ? modes[firstLive - 1].name : null;
			for (let i = firstLive + 1; i < modes.length; ++i) {
				if (isBaked(modes[i].mode)) {
					warnings.push(
						`${modes[i].name}: capa cocida por encima de "${modes[firstLive].name}" viva; puede tapar la capa viva`,
					);
				}
			}
		}
	}

	const legacyPatch = plan?.animations?.mode === 'patch';
	const liveLayers = new Set(plan?.animations?.layers ?? []);
	const liveObjects = new Set(plan?.animations?.objects ?? []);
	for (const item of modes) {
		const declared = item.kind === 'object' ? liveObjects.has(item.name) : liveLayers.has(item.name);
		if (declared && !isLive(item.mode)) {
			const message = `${item.name}: aparece en animations pero su modo es "${item.mode}"`;
			if (legacyPatch) warnings.push(`${message} (legacy patch: base con primer frame)`);
			else errors.push(message);
		}
	}
	for (const name of [...liveLayers, ...liveObjects]) {
		const item = modes.find((candidate) => candidate.name === name);
		if (item == null) errors.push(`animations: no existe la capa "${name}"`);
	}

	const opaque = plan?.render?.opaque ?? [];
	for (const name of opaque) {
		const item = modes.find((candidate) => candidate.name === name);
		if (item == null) errors.push(`render.opaque: no existe la capa "${name}"`);
	}

	const budget = plan?.animation_budget ?? null;
	if (budget != null) {
		const policy = budget.policy ?? 'preserve50';
		if (!['preserve50', 'target', 'off'].includes(policy)) {
			errors.push(`animation_budget.policy desconocida "${policy}"`);
		}
		const order = budget.degrade_order ?? [];
		for (const name of order) {
			const item = modes.find((candidate) => candidate.name === name);
			if (item == null) errors.push(`animation_budget.degrade_order: no existe "${name}"`);
			else if (!isLive(item.mode)) warnings.push(`${name}: en degrade_order pero su capa no es viva`);
		}
	}

	const occ = plan?.render?.occluders ?? [];
	for (const name of occ) {
		const item = modes.find((candidate) => candidate.name === name);
		if (item == null) errors.push(`render.occluders: no existe la capa "${name}"`);
		else if (item.mode !== 'occluder') {
			errors.push(`${name}: está en render.occluders pero su modo es "${item.mode}"`);
		}
	}

	return {
		ok: errors.length === 0,
		errors,
		warnings,
		baked_through: bakedThrough,
		modes: modes.map(({ name, kind, mode }) => ({ name, kind, mode })),
	};
}

function main() {
	const args = process.argv.slice(2);
	if (args.length < 2) {
		console.error('Uso: node tools/tiled/validate-cook-plan.mjs <mapa.tmx> <cook-plan.json>');
		process.exit(2);
	}
	const ir = parseTmxFile(path.resolve(args[0]));
	const plan = JSON.parse(fs.readFileSync(path.resolve(args[1]), 'utf8'));
	const report = validateCookPlan(ir, plan);
	for (const warning of report.warnings) console.warn(`[plan] aviso: ${warning}`);
	for (const error of report.errors) console.error(`[plan] error: ${error}`);
	console.log(`[plan] baked_through=${report.baked_through ?? '-'} ok=${report.ok}`);
	process.exit(report.ok ? 0 : 1);
}

if (process.argv[1] != null && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
	main();
}
