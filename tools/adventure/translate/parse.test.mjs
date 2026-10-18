// Tests del parser descumm -> AST (`adv-ast/1`): gramática representativa del corpus real.
// Uso: node --test tools/adventure/translate/parse.test.mjs

import test from 'node:test';
import assert from 'node:assert/strict';
import { parseDescummText } from './descumm.mjs';

const SAMPLE = `[0000] (2C) CursorHide();
[0002] (2C) UserputOff();
[0004] (48) if (Local[0] == -7873) {
[000B] (1A)   Local[0] = 0;
[0010] (**) }
[0010] (33) loadString(27,"monkey2.cfg");
[002C] (27) Local[1] = GetStringChar(27,0);
[0032] (9A) VAR_RESULT = Local[1];
[0037] (17) VAR_RESULT &= 1;
[003C] (A8) if (VAR_RESULT) {
[0041] (1A)   Bit[587] = 1;
[0046] (18) } else {
[0049] (1A)   Bit[587] = 0;
[004E] (**) }
[004E] (9A) VAR_RESULT = Local[1];
[0053] (17) VAR_RESULT &= 4;
[0058] (A8) if (VAR_RESULT) {
[005D] (1A)   Bit[588] = 1;
[0062] (18) } else {
[0065] (1A)   Bit[588] = 0;
[006A] (**) }
[006A] (38) if (VAR_SOUNDCARD >= 3) {
[0071] (27)   VAR_RESULT = GetStringChar(27,1);
[0077] (48)   if (VAR_RESULT == 0) {
[007E] (4C)     soundKludge([6,96]);
[0086] (18)   } else {
[0089] (4C)     soundKludge([6,VAR_RESULT]);
[0091] (**) }
[0091] (**) }
[0091] (1A) VAR_MACHINE_SPEED = 2;
[0096] (1A) Var[597] = 1;
[009B] (27) CreateString(42,105);
[00A3] (1A) VAR_TIMER_NEXT = 0;
[00A8] (27) PutCodeInString(22,"@@@");
[00B7] (27) SetStringChar(42,12,29);
[00C1] (13) ActorOps(7,[Init(),Costume(75),Name("Herman Toothrot"),TalkColor(14)]);
[00D0] (14) print(253,[Text("MIDI seleccionado.")]);
[00E0] (AC) Exprmode Local[4] = (Local[0] - Local[1]);
[00F0] (AC) Exprmode Local[7] = ((<VAR_RESULT = getActorWidth(Local[2])> / 2) + 4);
[0100] (18) goto 0030;
[0110] (80) breakHere();
[0111] (00) stopObjectCode();
[0120] (46) Var[100]++;
[0122] (46) Local[0]--;
[0124] (48) unless (Var[100] > 22) goto 0073;
[0128] (27) PutCodeInString(39,);
[0129] (**) /* goto 001E; */
`;

test('descumm: sentencias, bloques if/else anidados y expresiones', () => {
	const ast = parseDescummText(SAMPLE, { script: 'scrp_test', room: 1 });
	assert.equal(ast.format, 'adv-ast/1');
	assert.equal(ast.script, 'scrp_test');
	assert.equal(ast.room, 1);
	assert.equal(ast.stats.raw, 0, 'el fixture debe parsearse al 100 %');
	assert.ok(ast.stats.lines > 40);
	const s = ast.statements;
	// CursorHide / UserputOff como llamadas.
	assert.deepEqual(s[0], { kind: 'call', name: 'CursorHide', args: [], addr: 0, op: 0x2c });
	// if anidado con else: Local[0] == -7873
	assert.equal(s[2].kind, 'if');
	assert.equal(s[2].cond.k, 'bin');
	assert.equal(s[2].cond.op, '==');
	assert.equal(s[2].then[0].kind, 'assign');
	assert.equal(s[2].then[0].target.k, 'var');
	assert.equal(s[2].then[0].value.v, 0);
	// Asignación con operador compuesto: `aop` es el operador, `op` el byte de opcode.
	const andAssign = s.find((x) => x.kind === 'assign' && x.aop === '&=');
	assert.ok(andAssign, 'asignación compuesta &= con aop');
	assert.equal(andAssign.op, 0x17);
	// if con else (condición simple) y su rama alternativa.
	const ifVar = s.find((x) => x.kind === 'if' && x.addr === 0x3c);
	assert.ok(ifVar && Array.isArray(ifVar.else) && ifVar.else.length === 1);
	// ActorOps: llamada con array de llamadas anidadas.
	const actorOps = s.find((x) => x.kind === 'call' && x.name === 'ActorOps');
	assert.ok(actorOps);
	assert.equal(actorOps.args[1].k, 'arr');
	assert.deepEqual(actorOps.args[1].items.map((i) => i.name), ['Init', 'Costume', 'Name', 'TalkColor']);
	// print con texto.
	const print = s.find((x) => x.kind === 'call' && x.name === 'print');
	assert.equal(print.args[1].items[0].args[0].v, 'MIDI seleccionado.');
	// Exprmode e inline assignment.
	const expr = s.find((x) => x.kind === 'assign' && x.exprmode === true);
	assert.ok(expr);
	const inline = s.find((x) => x.kind === 'assign' && JSON.stringify(x.value).includes('"inline"'));
	assert.ok(inline, 'la asignación inline <...> se conserva en el AST');
	assert.equal(inline.value.k, 'bin');
	// goto con destino hexadecimal.
	const gt = s.find((x) => x.kind === 'goto');
	assert.equal(gt.target, 0x30);
	// Formas propias de descumm: ++/--, unless-goto, comentario y coma final.
	assert.ok(s.some((x) => x.kind === 'incdec' && x.aop === '++' && x.target.k === 'var'));
	assert.ok(s.some((x) => x.kind === 'incdec' && x.aop === '--'));
	const unless = s.find((x) => x.kind === 'unless-goto');
	assert.ok(unless && unless.target === 0x73);
	assert.ok(s.some((x) => x.kind === 'comment' && x.text === 'goto 001E;'));
	const pcs = s.find((x) => x.kind === 'call' && x.name === 'PutCodeInString' && x.addr === 0x128);
	assert.equal(pcs.args.length, 1, 'la coma final no inventa un argumento');
	// Terminadores del original.
	assert.ok(s.some((x) => x.kind === 'call' && x.name === 'breakHere'));
	assert.ok(s.some((x) => x.kind === 'call' && x.name === 'stopObjectCode'));
});

test('descumm: lo no reconocido queda como raw (cobertura honesta)', () => {
	const ast = parseDescummText('[0000] (01) ??? esto_no_es_valido ???;\n[0002] (00) stopObjectCode();\n', { script: 'x', room: 2 });
	assert.equal(ast.stats.lines, 2);
	assert.equal(ast.stats.raw, 1);
	assert.equal(ast.statements[0].kind, 'raw');
	assert.equal(ast.statements[0].text, '??? esto_no_es_valido ???;');
	assert.equal(ast.statements[1].kind, 'call');
});

test('descumm: cadenas con comillas escapadas y CRLF', () => {
	const text = '[0000] (14) print(1,[Text("El dijo \\"hola\\".")]);\r\n';
	const ast = parseDescummText(text, { script: 's', room: 3 });
	assert.equal(ast.stats.raw, 0);
	assert.equal(ast.statements[0].args[1].items[0].args[0].v, 'El dijo "hola".');
});
