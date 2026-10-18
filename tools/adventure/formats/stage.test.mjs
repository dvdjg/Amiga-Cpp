// Tests del formato `adv-stage/1` (tools/adventure/formats/stage.mjs): andamio válido,
// determinismo y rechazo de violaciones de hardware/presupuesto propias de A500.
// Uso: node --test tools/adventure/formats/stage.test.mjs

import test from 'node:test';
import assert from 'node:assert/strict';
import { STAGE_FORMAT, scaffoldStage, validateStage } from './stage.mjs';

const room = {
	room: 4,
	name: 'monkey-3',
	width: 320,
	height: 200,
	palette: { kind: 'clut', colors: Array.from({ length: 256 }, (_, i) => [i & 63, (i * 2) & 63, (i * 3) & 63]) },
};

test('stage: el andamio del SIM es válido, con muestras de paleta y determinista', () => {
	const st = scaffoldStage(room, 'mi1');
	assert.equal(st.format, STAGE_FORMAT);
	assert.deepEqual(validateStage(st), []);
	assert.equal(st.palette.samples.length, 16);
	assert.deepEqual(st.palette.samples[0], '#000');
	assert.deepEqual(scaffoldStage(room, 'mi1'), st, 'determinista');
	// Scaffold de sala alta recorta la banda al display (256).
	const tall = scaffoldStage({ ...room, height: 300 }, 'mi1');
	assert.deepEqual(validateStage(tall), []);
	assert.equal(tall.display.height, 256);
});

test('stage: rechaza violaciones de hardware/presupuesto (A500)', () => {
	const base = () => scaffoldStage(room, 'mi1');

	{
		const st = base();
		st.display.mode = 'ehb';
		st.display.planes = 5;
		assert.ok(validateStage(st).some((e) => e.includes('EHB')));
	}
	{
		const st = base();
		st.display.planes = 7;
		assert.ok(validateStage(st).some((e) => e.includes('planes')));
	}
	{
		const st = base();
		st.sprites.channels.push({ channel: 0, role: 'otra' });
		assert.ok(validateStage(st).some((e) => e.includes('duplicado')));
	}
	{
		const st = base();
		st.sprites.channels.push({ channel: 8, role: 'fuera' });
		assert.ok(validateStage(st).some((e) => e.includes('channel')));
	}
	{
		const st = base();
		st.layers[0].parallax = 2048;
		assert.ok(validateStage(st).some((e) => e.includes('parallax')));
	}
	{
		const st = base();
		st.layers[0].band = { top: 200, height: 100 };
		assert.ok(validateStage(st).some((e) => e.includes('band')));
	}
	{
		const st = base();
		st.hud.region = { top: 240, height: 48 };
		assert.ok(validateStage(st).some((e) => e.includes('hud.region')));
	}
	{
		const st = base();
		st.copper.cues.push({
			id: 'sunset',
			trigger: { kind: 'onEnter' },
			frames: 100,
			tracks: [{ kind: 'palette', first: 28, count: 7, keyframes: [{ frame: 0, colors: ['#000', '#001', '#002', '#003', '#004', '#005', '#006'] }] }],
		});
		assert.ok(validateStage(st).some((e) => e.includes('rango de paleta')));
	}
	{
		const st = base();
		st.copper.cues.push({
			id: 'bad',
			trigger: { kind: 'onEnter' },
			frames: 10,
			tracks: [{ kind: 'reg', line: 300, reg: 'BPLCON1', value: 0 }],
		});
		assert.ok(validateStage(st).some((e) => e.includes('reg.line')));
	}
});

test('stage: un cue de paleta correcto valida', () => {
	const st = scaffoldStage(room, 'mi1');
	st.copper.cues.push({
		id: 'sunset',
		trigger: { kind: 'onScrollX', value: 480 },
		frames: 1500,
		restore: true,
		tracks: [{ kind: 'palette', first: 1, count: 7, keyframes: [{ frame: 0, colors: Array(7).fill('#000') }, { frame: 1500, colors: Array(7).fill('#f80') }] }],
	});
	assert.deepEqual(validateStage(st), []);
});
