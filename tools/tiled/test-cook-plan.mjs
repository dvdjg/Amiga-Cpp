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

console.log('OK: cook-plan contract');
