#!/usr/bin/env node
import assert from 'node:assert/strict';
import { validateCookPlan } from './validate-cook-plan.mjs';

const ir = {
	layers: [
		{ kind: 'tile', name: 'Ground' },
		{ kind: 'tile', name: 'Water' },
		{ kind: 'group', name: 'Foreground', layers: [{ kind: 'tile', name: 'Trees' }] },
	],
};

const valid = {
	render: {
		baked_through: 'Ground',
		modes: { Ground: 'baked', Water: 'animated', Trees: 'occluder' },
		occluders: ['Trees'],
	},
	layers: { Ground: 'baked', Water: 'animated' },
	animations: { mode: 'live', layers: ['Water'] },
};
const report = validateCookPlan(ir, valid);
assert.equal(report.ok, true, report.errors.join('; '));
assert.equal(report.baked_through, 'Ground');
assert.equal(report.errors.length, 0);

const bad = {
	render: { baked_through: 'Ground' },
	layers: { Ground: 'baked', Water: 'animated', Trees: 'baked' },
	animations: { mode: 'live', layers: ['Water'] },
};
const badReport = validateCookPlan(ir, bad);
assert.equal(badReport.ok, false);
assert.ok(badReport.errors.some((message) => message.includes('Trees')));

const legacy = {
	layers: { Ground: 'baked', Water: 'baked' },
	animations: { mode: 'patch', layers: ['Water'] },
};
const legacyReport = validateCookPlan(ir, legacy);
assert.equal(legacyReport.ok, true);
assert.ok(legacyReport.warnings.some((message) => message.includes('legacy patch')));

// Reglas por objeto: id/gid/clase y propiedades de la copia de cocina.
const objectIr = {
	layers: [
		{ kind: 'tile', name: 'Ground' },
		{
			kind: 'object',
			name: 'Objects',
			objects: [
				{ id: 1, gid: 477, class: 'Building', name: '', type: '', rotation: 0, properties: [] },
				{ id: 2, gid: 516, class: '', name: '', type: '', rotation: 0, properties: [] },
				{
					id: 3, gid: 490, class: '', name: '', type: '', rotation: 0,
					properties: [{ name: 'cook', value: 'foreground' }, { name: 'sort_y', value: 'top' }],
				},
			],
		},
	],
};
const objectPlan = {
	render: { baked_through: 'Ground', modes: { Ground: 'baked', Objects: 'foreground' } },
	layers: { Ground: 'baked' },
	objects: {
		Objects: {
			default: 'baked',
			rules: [
				{ id: [2], cook: 'foreground', sort_y: 'base' },
				{ class: 'Building', cook: 'baked' },
			],
		},
	},
};
const objectReport = validateCookPlan(objectIr, objectPlan);
assert.equal(objectReport.ok, true, objectReport.errors.join('; '));
assert.equal(objectReport.object_roles.foreground, 2);
assert.equal(objectReport.object_roles.baked, 1);

const badObjectPlan = {
	...objectPlan,
	objects: {
		Objects: {
			default: 'baked',
			rules: [{ gid: [516], cook: 'desconocido', sort_y: 'center', occluder: 'hojas' }],
		},
	},
};
const badObjectReport = validateCookPlan(objectIr, badObjectPlan);
assert.equal(badObjectReport.ok, false);
assert.ok(badObjectReport.errors.some((message) => message.includes('cook desconocido')));
assert.ok(badObjectReport.errors.some((message) => message.includes('sort_y desconocido')));
assert.ok(badObjectReport.errors.some((message) => message.includes('occluder desconocido')));

const bitplanePlan = { ...objectPlan, render: { ...objectPlan.render, shadow_mode: 'bitplane' } };
const bitplaneReport = validateCookPlan(objectIr, bitplanePlan);
assert.equal(bitplaneReport.ok, false);
assert.ok(bitplaneReport.errors.some((message) => message.includes('bitplane')));

console.log('OK: cook-plan contract');
