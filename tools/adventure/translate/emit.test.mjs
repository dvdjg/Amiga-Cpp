// Tests de la traducción AST -> C++ (`translate/cpp.mjs`): vocabulario, réplicas y omisión
// segura de control de flujo no estructurado.
// Uso: node --test tools/adventure/translate/emit.test.mjs

import test from 'node:test';
import assert from 'node:assert/strict';
import { parseDescummText } from './descumm.mjs';
import { translateScript, TranslateError } from './cpp.mjs';

const SRC = `[0000] (2C) CursorHide();
[0002] (14) print(253,[Text("Hola, pirata!")]);
[0010] (48) if (VAR_RESULT == 3) {
[0018] (1E) walkActorTo(7,120,141);
[0022] (18) } else {
[0028] (1A) VAR_RESULT = 0;
[0030] (**) }
[0032] (46) Var[100]++;
[0036] (9A) Local[2] = getObjectState(124);
[0040] (AC) Exprmode Local[0] = (<VAR_RESULT = getActorWidth(Local[2])> + 1);
[0050] (1C) startSound(123);
[0055] (1A) Bit[587] = 1;
[0060] (00) stopObjectCode();
`;

test('emit: traduce llamadas, if/else, asignaciones y replicas', () => {
	const ast = parseDescummText(SRC, { script: 'SCRP_4_218673', room: 4 });
	const { name, code, stats } = translateScript(ast);
	assert.equal(name, 'scrp_4_SCRP_4_218673');
	assert.ok(code.startsWith('// Generado'));
	assert.ok(code.includes('template <class W>'));
	assert.ok(code.includes('void scrp_4_SCRP_4_218673(W& w)'));
	assert.ok(code.includes('w.cursor_hide();'));
	assert.ok(code.includes('w.say(253, "Hola, pirata!");'));
	assert.ok(code.includes('if ((w.var_name("VAR_RESULT") == 3)) {'));
	assert.ok(code.includes('w.walk_to(7, 120, 141);'));
	assert.ok(code.includes('} else {'));
	assert.ok(code.includes('w.var_name("VAR_RESULT") = 0;'));
	assert.ok(code.includes('w.var(100)++;'));
	assert.ok(code.includes('w.local(2) = w.get_object_state(124);'));
	assert.ok(code.includes('(w.var_name("VAR_RESULT") = w.actor_width(w.local(2)))'));
	assert.ok(code.includes('w.start_sound(123);'));
	assert.ok(code.includes('w.bit(587) = 1;'));
	assert.ok(code.includes('w.stop();'));
	assert.equal(stats.unsupported, 0, 'el fixture se traduce sin llamadas no soportadas');
	assert.equal(stats.sayLines, 1);
});

test('emit: primitivas no mapeadas se contabilizan y no rompen compilacion', () => {
	const ast = parseDescummText('[0000] (13) ActorOps(7,[Init(),Costume(75)]);\n[0002] (00) stopObjectCode();\n', { script: 'x', room: 5 });
	const { code, stats } = translateScript(ast);
	assert.ok(code.includes('w.unsupported("ActorOps");'));
	assert.equal(stats.unsupported, 1);
});

test('emit: goto/unless-goto a etiquetas C++ y raw contabilizado (sin romper)', () => {
	const g = parseDescummText(
		'[0000] (48) if (Var[1] > 2) {\n[0004] (18) goto 000C;\n[0006] (1A) Var[1] = 0;\n[0008] (**) }\n[000C] (00) stopObjectCode();\n',
		{ script: 'g', room: 6 },
	);
	const { code } = translateScript(g);
	assert.ok(code.includes('goto L_C;'));
	assert.ok(code.includes('L_C: ;'));
	const u = parseDescummText(
		'[0000] (48) unless (Var[1] > 2) goto 000C;\n[0002] (1A) Var[1] = 0;\n[000C] (00) stopObjectCode();\n',
		{ script: 'u', room: 6 },
	);
	assert.ok(translateScript(u).code.includes('if (!(w.var(1) > 2)) goto L_C;'));
	const r = parseDescummText('[0000] (01) ???;\n[0002] (00) stopObjectCode();\n', { script: 'r', room: 6 });
	const rr = translateScript(r);
	assert.ok(rr.code.includes('w.unsupported("raw: ???;");'));
	assert.equal(rr.stats.unsupported, 1);
});

test('emit: goto a una dirección sin sentencia omite el guion', () => {
	const bad = parseDescummText('[0000] (18) goto 00FF;\n[0002] (00) stopObjectCode();\n', { script: 'b', room: 6 });
	assert.throws(() => translateScript(bad), (e) => e instanceof TranslateError && /goto a dirección sin sentencia/.test(e.reason));
});
