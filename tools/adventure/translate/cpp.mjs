// Reglas de traducción **AST → C++ de `eng::adv`** (I1, 2/2). El código generado son
// plantillas `template <class W> void scrp_...(W& w)` que usan el contrato `script_hooks.hpp`
// (variables locales/globales/bits y primitivas de aventura). Lo no traducible se marca y el
// guion se omite entero si su control de flujo no es seguro (goto/unless-goto/raw): la
// cobertura se informa, no se maquilla.

// Nombre de primitiva SCUMM (descumm) → método del mundo (`eng::adv::NullScriptWorld` y el real).
export const PRIMITIVES = new Map([
	['walkActorTo', 'walk_to'],
	['walkActorToActor', 'walk_to_actor'],
	['walkActorToObject', 'walk_to_object'],
	['putActor', 'put_actor'],
	['putActorInRoom', 'put_actor_in_room'],
	['putActorAtObject', 'put_actor_at_object'],
	['animateActor', 'animate'],
	['animateCostume', 'animate'],
	['faceActor', 'face_actor'],
	['startScript', 'start_script'],
	['stopScript', 'stop_script'],
	['loadRoom', 'change_room'],
	['loadRoomWithEgo', 'change_room_with_ego'],
	['setState', 'set_state'],
	['getObjectState', 'get_object_state'],
	['getObjectOwner', 'get_object_owner'],
	['setOwnerOf', 'set_owner'],
	['pickupObject', 'pick_up'],
	['setClass', 'set_class'],
	['startSound', 'start_sound'],
	['stopSound', 'stop_sound'],
	['startMusic', 'start_music'],
	['stopMusic', 'stop_music'],
	['delay', 'delay'],
	['delayVariable', 'delay_var'],
	['panCameraTo', 'pan_camera'],
	['setCameraAt', 'set_camera'],
	['drawObject', 'draw_object'],
	['cursorCommand', 'cursor'],
	['CursorHide', 'cursor_hide'],
	['UserputOn', 'userput'],
	['UserputOff', 'userput'],
	['getRandomNr', 'random'],
	['getActorRoom', 'actor_room'],
	['getActorX', 'actor_x'],
	['getActorY', 'actor_y'],
	['getActorMoving', 'actor_moving'],
	['getActorWidth', 'actor_width'],
	['getVerbEntryPoint', 'verb_entrypoint'],
	['getInventoryCount', 'inventory_count'],
	['isScriptRunning', 'script_running'],
	['beginOverride', 'begin_override'],
	['endOverride', 'end_override'],
	['stopObjectCode', 'stop'],
	['breakHere', 'break_here'],
	['printEgo', 'say_ego'],
	['WaitForMessage', 'wait_message'],
	['WaitForActor', 'wait_actor'],
	['cutscene', 'cutscene'],
	['endCutscene', 'end_cutscene'],
	['setBoxFlags', 'set_box_flags'],
	['SetStringChar', 'set_string_char'],
	['Resource.loadScript', 'load_resource'],
	['Resource.lockScript', 'lock_resource'],
]);

/** Error de traducción de un guion (motivo por el que se omite). */
export class TranslateError extends Error {
	constructor(reason) {
		super(reason);
		this.reason = reason;
	}
}

const cstr = (s) => JSON.stringify(String(s));

function expr(node, ctx) {
	switch (node.k) {
	case 'num':
		return String(node.v);
	case 'str':
		ctx.unsupported++;
		return cstr(node.v);
	case 'name':
		if (/^VAR_[A-Z0-9_]+$/.test(node.v)) return `w.var_name(${cstr(node.v)})`;
		ctx.consts++;
		return `w.const_name(${cstr(node.v)})`;
	case 'var':
		if (node.space === 'local') return `w.local(${expr(node.index, ctx)})`;
		if (node.space === 'bit') return `w.bit(${expr(node.index, ctx)})`;
		return `w.var(${expr(node.index, ctx)})`;
	case 'un':
		return `(${node.op}${expr(node.a, ctx)})`;
	case 'bin':
		return `(${expr(node.a, ctx)} ${node.op} ${expr(node.b, ctx)})`;
	case 'inline': {
		const s = node.stmt;
		return `(${expr(s.target, ctx)} ${s.aop} ${expr(s.value, ctx)})`;
	}
	case 'call': {
		const method = PRIMITIVES.get(node.name);
		if (!method) {
			ctx.unsupported++;
			return `w.unsupported_value(${cstr(node.name)})`;
		}
		if (node.args.some((a) => a.k === 'arr' || a.k === 'str')) {
			ctx.unsupported++;
			return `w.unsupported_value(${cstr(node.name)})`;
		}
		ctx.mapped++;
		return `w.${method}(${node.args.map((a) => expr(a, ctx)).join(', ')})`;
	}
	case 'arr':
		ctx.unsupported++;
		return '0';
	default:
		throw new TranslateError(`expresión no soportada: ${node.k}`);
	}
}

// `print(actor,[Text("...")])` → `w.say(actor,"...")` (primera réplica; varias → una llamada
// por texto). Devuelve la lista de sentencias o null si no es el patrón esperado.
function printSay(node, ctx) {
	const args = node.args;
	if (args.length !== 2 || args[1].k !== 'arr') return null;
	const out = [];
	for (const item of args[1].items) {
		if (item.k === 'call' && item.name === 'Text' && item.args.length === 1 && item.args[0].k === 'str') {
			out.push(`w.say(${expr(args[0], ctx)}, ${cstr(item.args[0].v)});`);
		} else {
			return null;
		}
	}
	ctx.sayLines += out.length;
	return out.length ? out : null;
}

function emitCall(node, indent, ctx, lines) {
	const say = printSay(node, ctx);
	if (say) {
		for (const s of say) lines.push(`${indent}${s}`);
		return;
	}
	const method = PRIMITIVES.get(node.name);
	if (!method) {
		ctx.unsupported++;
		lines.push(`${indent}w.unsupported(${cstr(node.name)});`);
		return;
	}
	if (node.args.some((a) => a.k === 'arr' || a.k === 'str')) {
		ctx.unsupported++;
		lines.push(`${indent}w.unsupported(${cstr(node.name)});`);
		return;
	}
	ctx.mapped++;
	lines.push(`${indent}w.${method}(${node.args.map((a) => expr(a, ctx)).join(', ')});`);
}

function emitStatements(stmts, indent, ctx, lines, labels) {
	for (const s of stmts) {
		if (labels && s.addr !== undefined && labels.has(s.addr)) {
			lines.push(`${indent}L_${s.addr.toString(16).toUpperCase()}: ;`);
		}
		switch (s.kind) {
		case 'call':
			emitCall(s, indent, ctx, lines);
			break;
		case 'assign':
			if (!s.aop) throw new TranslateError('assign sin operador');
			if (s.exprmode) ctx.exprmode++;
			lines.push(`${indent}${expr(s.target, ctx)} ${s.aop} ${expr(s.value, ctx)};`);
			break;
		case 'incdec':
			lines.push(`${indent}${expr(s.target, ctx)}${s.aop};`);
			break;
		case 'if': {
			lines.push(`${indent}if (${expr(s.cond, ctx)}) {`);
			emitStatements(s.then ?? [], indent + '\t', ctx, lines, labels);
			if (s.else) {
				lines.push(`${indent}} else {`);
				emitStatements(s.else, indent + '\t', ctx, lines, labels);
			}
			lines.push(`${indent}}`);
			break;
		}
		case 'comment':
			lines.push(`${indent}// ${String(s.text).replace(/[\r\n]+/g, ' ')}`);
			break;
		case 'expr':
			lines.push(`${indent}(void)${expr(s.expr, ctx)};`);
			break;
		case 'goto':
			lines.push(`${indent}goto L_${Number(s.target).toString(16).toUpperCase()};`);
			break;
		case 'unless-goto':
			lines.push(`${indent}if (!${expr(s.cond, ctx)}) goto L_${Number(s.target).toString(16).toUpperCase()};`);
			break;
		case 'raw':
			// Línea no parseada (p. ej. salida corrupta de descumm): se contabiliza y no rompe.
			ctx.unsupported++;
			lines.push(`${indent}w.unsupported(${cstr(`raw: ${String(s.text ?? '').slice(0, 60)}`)});`);
			break;
		default:
			throw new TranslateError(`sentencia no soportada: ${s.kind}`);
		}
	}
}

const sanitize = (s) => String(s).replace(/[^A-Za-z0-9_]/g, '_').replace(/^(\d)/, '_$1');

/**
 * Traduce un AST `adv-ast/1` a una función C++ (texto) o lanza `TranslateError`.
 * \param ast AST de un guion (`parseDescummText`).
 * \return { name, code, stats } con la función generada y contadores de cobertura.
 */
export function translateScript(ast) {
	const ctx = { mapped: 0, unsupported: 0, consts: 0, sayLines: 0, exprmode: 0 };
	const lines = [];
	const name = `scrp_${ast.room}_${sanitize(ast.script)}`;
	try {
		// Etiquetas de los destinos de `goto` (solo se emiten las usadas). Se valida que el
		// destino exista como sentencia; si no, el guion se omite (no se inventa el flujo).
		const labels = new Set();
		const addrs = new Set();
		const walk = (list) => {
			for (const s of list) {
				if (s.addr !== undefined) addrs.add(s.addr);
				if (s.kind === 'goto' || s.kind === 'unless-goto') labels.add(Number(s.target));
				if (s.then) walk(s.then);
				if (s.else) walk(s.else);
			}
		};
		walk(ast.statements);
		for (const target of labels) {
			if (!addrs.has(target)) throw new TranslateError(`goto a dirección sin sentencia (0x${target.toString(16)})`);
		}
		emitStatements(ast.statements, '\t', ctx, lines, labels);
	} catch (err) {
		if (err instanceof TranslateError) throw err;
		throw new TranslateError(err.message);
	}
	const code = [
		`// Generado por translate/emit.mjs — guion ${ast.script} (sala ${ast.room}), ${ast.stats?.lines ?? '?'} opcodes.`,
		`// Cobertura: ${ctx.mapped} llamadas traducidas, ${ctx.unsupported} no soportadas, ${ctx.sayLines} réplicas, ${ctx.consts} constantes.`,
		`template <class W>`,
		`void ${name}(W& w) {`,
		...lines,
		`}`,
		'',
	].join('\n');
	return { name, code, stats: ctx };
}
