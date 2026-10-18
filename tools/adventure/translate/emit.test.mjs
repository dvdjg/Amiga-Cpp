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

test('emit: ActorOps/VerbOps -> una llamada tipada por sub-operacion', () => {
	const ast = parseDescummText(
		'[0000] (13) ActorOps(7,[Init(),Costume(75),Name("Guybrush"),TalkColor(14)]);\n[0002] (00) stopObjectCode();\n',
		{ script: 'a', room: 5 },
	);
	const { code, stats } = translateScript(ast);
	assert.ok(code.includes('w.actor_op(7, eng::adv::ActorOp::init);'));
	assert.ok(code.includes('w.actor_op(7, eng::adv::ActorOp::costume, 75);'));
	assert.ok(code.includes('w.actor_op(7, eng::adv::ActorOp::name, "Guybrush");'));
	assert.ok(code.includes('w.actor_op(7, eng::adv::ActorOp::talk_color, 14);'));
	assert.equal(stats.unsupported, 0);
	assert.equal(stats.mapped, 5, '4 sub-ops + stopObjectCode');

	const v = parseDescummText(
		'[0000] (7A) VerbOps(1,[SetXY(10,20),Text("Abrir"),On(),Text(sound(0x1234, 0xA) + "x")]);\n[0002] (00) stopObjectCode();\n',
		{ script: 'v', room: 5 },
	);
	const vv = translateScript(v);
	assert.ok(vv.code.includes('w.verb_op(1, eng::adv::VerbOp::set_xy, 10, 20);'));
	assert.ok(vv.code.includes('w.verb_op(1, eng::adv::VerbOp::text, "Abrir");'));
	assert.ok(vv.code.includes('w.verb_op(1, eng::adv::VerbOp::on);'));
	assert.ok(vv.code.includes('w.unsupported("VerbOps:Text");'), 'Text dinamico se marca, no rompe');
	assert.equal(vv.stats.unsupported, 1);
	assert.equal(vv.stats.mapped, 4, '3 sub-ops + stopObjectCode');
});

test('emit: print/printEgo estaticos -> replicas; listas y consts -> w.list', () => {
	const p = parseDescummText(
		'[0000] (94) print([Text("Sin actor")]);\n[0002] (94) printEgo([Text("Ego")]);\n[0004] (94) print(255,[Pos(160,8),Center(),Text("Centrado")]);\n[0006] (94) print(4,[Text("A"),Text("B")]);\n[0008] (00) stopObjectCode();\n',
		{ script: 'p', room: 5 },
	);
	const pp = translateScript(p);
	assert.ok(pp.code.includes('w.say_ego("Sin actor");'));
	assert.ok(pp.code.includes('w.say_ego("Ego");'));
	assert.ok(pp.code.includes('w.say(255, "Centrado");'));
	assert.ok(pp.code.includes('w.say(4, "A");'));
	assert.ok(pp.code.includes('w.say(4, "B");'));
	assert.equal(pp.stats.sayLines, 5);
	assert.equal(pp.stats.unsupported, 0);

	const dyn = parseDescummText('[0000] (94) print(255,[Text(getString(VAR_INVENTORY_SCRIPT))]);\n[0002] (00) stopObjectCode();\n', { script: 'd', room: 5 });
	const dd = translateScript(dyn);
	assert.ok(dd.code.includes('w.unsupported("print");'), 'Text dinamico cae a no soportado');
	assert.equal(dd.stats.unsupported, 1);

	const s = parseDescummText(
		'[0000] (6A) startScript(45,[3],F,R);\n[0002] (0A) startScript(200,[]);\n[0004] (2B) setClass(7,[133,141]);\n[0006] (CE) cutscene([2]);\n[0008] (00) stopObjectCode();\n',
		{ script: 's', room: 5 },
	);
	const ss = translateScript(s);
	assert.ok(ss.code.includes('w.start_script(45, w.list(3), w.const_name("F"), w.const_name("R"));'));
	assert.ok(ss.code.includes('w.start_script(200, w.list());'));
	assert.ok(ss.code.includes('w.set_class(7, w.list(133, 141));'));
	assert.ok(ss.code.includes('w.cutscene(w.list(2));'));
	assert.equal(ss.stats.unsupported, 0);

	const r = parseDescummText('[0000] (CE) setVarRange(Var[250],9,[0,0,1]);\n[0002] (00) stopObjectCode();\n', { script: 'r', room: 5 });
	assert.ok(translateScript(r).code.includes('w.set_var_range(w.var(250), 9, w.list(0, 0, 1));'));
});

test('emit: cadenas con escapes \\xNN de descumm se decodifican al caracter', () => {
	const ast = parseDescummText('[0000] (9A) PutCodeInString(34,"M\\xA0s tarde");\n[0002] (00) stopObjectCode();\n', { script: 'e', room: 5 });
	const { code } = translateScript(ast);
	assert.ok(code.includes('w.put_code_in_string(34, "M\u00A0s tarde");'));
});

test('emit: primitivas no mapeadas se contabilizan y no rompen compilacion', () => {
	const ast = parseDescummText('[0000] (13) bogusOp(7);\n[0002] (00) stopObjectCode();\n', { script: 'x', room: 5 });
	const { code, stats } = translateScript(ast);
	assert.ok(code.includes('w.unsupported("bogusOp");'));
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
