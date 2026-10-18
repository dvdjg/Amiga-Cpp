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
	['Resource.unlockScript', 'unlock_script'],
	['Resource.loadCostume', 'load_costume'],
	['Resource.lockCostume', 'lock_costume'],
	['Resource.unlockCostume', 'unlock_costume'],
	['Resource.loadSound', 'load_sound'],
	['Resource.lockSound', 'lock_sound'],
	['Resource.unlockSound', 'unlock_sound'],
	['Resource.loadFlObject', 'load_fl_object'],
	['soundKludge', 'sound_kludge'],
	['doSentence', 'do_sentence'],
	['InitCharset', 'init_charset'],
	['setObjectName', 'set_object_name'],
	['CursorShow', 'cursor_show'],
	['CursorCommand', 'cursor_command'],
	['SetPalColor', 'set_pal_color'],
	['chainScript', 'chain_script'],
	['actorFollowCamera', 'actor_follow_camera'],
	['WaitForCamera', 'wait_camera'],
	['screenEffect', 'screen_effect'],
	['startObject', 'start_object'],
	['stopObject', 'stop_object'],
	['SetScreen', 'set_screen'],
	['setRGBRoomIntensity', 'set_rgb_room_intensity'],
	['RoomIntensity', 'room_intensity'],
	['drawBox', 'draw_box'],
	['ShakeOn', 'shake_on'],
	['ShakeOff', 'shake_off'],
	['saveVerbs', 'save_verbs'],
	['restoreVerbs', 'restore_verbs'],
	['PutCodeInString', 'put_code_in_string'],
	['setVarRange', 'set_var_range'],
]);

// Sub-operaciones de `ActorOps`/`VerbOps` (descompiladas por descumm) → enumerado tipado de
// `eng/adv/types.hpp`. Una llamada de guion se convierte en una `w.actor_op`/`w.verb_op` por
// sub-operación; las desconocidas o con argumentos dinámicos se marcan sin romper el guion.
const ACTOR_OPS = new Map([
	['Init', 'init'],
	['Costume', 'costume'],
	['Palette', 'palette'],
	['TalkColor', 'talk_color'],
	['TalkAnimNr', 'talk_anim_nr'],
	['IgnoreBoxes', 'ignore_boxes'],
	['Scale', 'scale'],
	['Elevation', 'elevation'],
	['NeverZClip', 'never_z_clip'],
	['WalkSpeed', 'walk_speed'],
	['SetZClip', 'set_z_clip'],
	['FollowBoxes', 'follow_boxes'],
	['Name', 'name'],
	['WalkAnimNr', 'walk_anim_nr'],
	['Sound', 'sound'],
	['Width', 'width'],
	['StandAnimNr', 'stand_anim_nr'],
	['InitAnimNr', 'init_anim_nr'],
	['AnimSpeed', 'anim_speed'],
]);

const VERB_OPS = new Map([
	['SetXY', 'set_xy'],
	['Key', 'key'],
	['Text', 'text'],
	['On', 'on'],
	['sound', 'sound'],
	['New', 'new_verb'],
	['Off', 'off'],
	['Color', 'color'],
	['HiColor', 'hi_color'],
	['Center', 'center'],
	['unknown8', 'unknown8'],
	['SetToObject', 'set_to_object'],
	['newline', 'newline'],
	['BackColor', 'back_color'],
	['Dim', 'dim'],
	['DimColor', 'dim_color'],
	['SetToString', 'set_to_string'],
	['x88', 'x88'],
	['Delete', 'delete_verb'],
	['startAnim', 'start_anim'],
]);

// Ítems de maquetación de `print([...])` cuyo efecto se ignora en el guion generado
// (posición/color/justificado): el texto plano (`Text`) es lo que se conserva.
const LAYOUT_ITEMS = new Set(['Pos', 'Center', 'Left', 'Overhead', 'Clipped', 'Color', 'Newline', 'newline', 'Wait', 'wait']);

/** Error de traducción de un guion (motivo por el que se omite). */
export class TranslateError extends Error {
	constructor(reason) {
		super(reason);
		this.reason = reason;
	}
}

const cstr = (s) => JSON.stringify(String(s));

// ¿La expresión contiene una cadena en algún nivel? Las concatenaciones de cadena
// (`"…" + getInt()`) no tienen traducción directa a C++ (serían `const char* + …`): un
// argumento con `str` que no sea un literal plano hace la llamada no traducible.
function hasString(node) {
	if (node.k === 'str') return true;
	if (node.k === 'un') return hasString(node.a);
	if (node.k === 'bin') return hasString(node.a) || hasString(node.b);
	if (node.k === 'inline') return hasString(node.value);
	return false;
}

// Argumentos de una llamada mapeada: un array de valores del original (`[3]`, `[]`) se
// materializa con `w.list(...)` (vive en la pila del guion); una cadena literal pasa tal
// cual; el resto son expresiones. Devuelve `null` si algún argumento no es traducible.
function callArgs(args, ctx) {
	const parts = [];
	for (const a of args) {
		if (a.k === 'arr') {
			const items = [];
			for (const item of a.items) {
				if (item.k === 'arr' || item.k === 'str') return null;
				items.push(expr(item, ctx));
			}
			parts.push(`w.list(${items.join(', ')})`);
		} else if (a.k === 'str') {
			parts.push(cstr(a.v));
		} else if (hasString(a)) {
			return null;
		} else {
			parts.push(expr(a, ctx));
		}
	}
	return parts;
}

// Expresiones argumentales admitidas en una sub-operación de ActorOps/VerbOps: valores
// directos (número/variable/constante), cadenas y combinaciones `un`/`bin` de ellos; una
// llamada dinámica en cualquier nivel (p. ej. `Text(sound(…) + "x")`) no es traducible.
function isStaticValue(node) {
	switch (node.k) {
	case 'num':
	case 'str':
	case 'name':
	case 'var':
		return true;
	case 'un':
		return isStaticValue(node.a);
	case 'bin':
		return isStaticValue(node.a) && isStaticValue(node.b);
	default:
		return false;
	}
}

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
		const args = callArgs(node.args, ctx);
		if (args === null) {
			ctx.unsupported++;
			return `w.unsupported_value(${cstr(node.name)})`;
		}
		ctx.mapped++;
		return `w.${method}(${args.join(', ')})`;
	}
	case 'arr':
		ctx.unsupported++;
		return '0';
	default:
		throw new TranslateError(`expresión no soportada: ${node.k}`);
	}
}

// `print([Text(…)])`/`print(actor,[Text(…)])`/`printEgo([Text(…)])` → serie de `say_ego`/`say`
// (una por texto). Los ítems de maquetación conocidos (`LAYOUT_ITEMS`) se ignoran; cualquier
// otro ítem o un `Text` dinámico hace la sentencia no traducible (devuelve null).
function printStatement(node, ctx) {
	if (node.name !== 'print' && node.name !== 'printEgo') return null;
	const args = node.args;
	let actor = null;
	let arr = null;
	if (args.length === 1 && args[0].k === 'arr') arr = args[0];
	else if (node.name === 'print' && args.length === 2 && args[1].k === 'arr') { actor = args[0]; arr = args[1]; }
	else return null;
	const out = [];
	for (const item of arr.items) {
		if (item.k !== 'call') return null;
		if (item.name === 'Text') {
			if (item.args.length !== 1 || item.args[0].k !== 'str') return null;
			out.push(actor ? `w.say(${expr(actor, ctx)}, ${cstr(item.args[0].v)});` : `w.say_ego(${cstr(item.args[0].v)});`);
		} else if (!LAYOUT_ITEMS.has(item.name)) {
			return null;
		}
	}
	if (!out.length) return null;
	ctx.sayLines += out.length;
	return out;
}

// `ActorOps(actor,[Init(),Costume(75),…])` / `VerbOps(n,[Text("…"),On(),…])` → una llamada
// `w.actor_op`/`w.verb_op` por sub-operación (enums de `eng/adv/types.hpp`). Cada sub-operación
// desconocida o con argumentos dinámicos se contabiliza y se marca, sin romper el guion.
function opsStatement(node, ctx, indent, lines) {
	const table = node.name === 'ActorOps' ? ACTOR_OPS : node.name === 'VerbOps' ? VERB_OPS : null;
	if (!table) return false;
	if (node.args.length !== 2 || node.args[1].k !== 'arr') return false;
	const target = expr(node.args[0], ctx);
	const method = node.name === 'ActorOps' ? 'actor_op' : 'verb_op';
	const enumName = node.name === 'ActorOps' ? 'ActorOp' : 'VerbOp';
	for (const item of node.args[1].items) {
		const args = item.k === 'call' && item.args.every(isStaticValue) ? callArgs(item.args, ctx) : null;
		if (item.k === 'call' && table.has(item.name) && args !== null) {
			ctx.mapped++;
			const extra = args.length ? `, ${args.join(', ')}` : '';
			lines.push(`${indent}w.${method}(${target}, eng::adv::${enumName}::${table.get(item.name)}${extra});`);
		} else {
			ctx.unsupported++;
			const what = item.k === 'call' ? item.name : item.k;
			lines.push(`${indent}w.unsupported(${cstr(`${node.name}:${what}`)});`);
		}
	}
	return true;
}

function emitCall(node, indent, ctx, lines) {
	const say = printStatement(node, ctx);
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
	const args = callArgs(node.args, ctx);
	if (args === null) {
		ctx.unsupported++;
		lines.push(`${indent}w.unsupported(${cstr(node.name)});`);
		return;
	}
	ctx.mapped++;
	lines.push(`${indent}w.${method}(${args.join(', ')});`);
}

function emitStatements(stmts, indent, ctx, lines, labels) {
	for (const s of stmts) {
		if (labels && s.addr !== undefined && labels.has(s.addr)) {
			lines.push(`${indent}L_${s.addr.toString(16).toUpperCase()}: ;`);
		}
		switch (s.kind) {
		case 'call':
			if (!opsStatement(s, ctx, indent, lines)) emitCall(s, indent, ctx, lines);
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
