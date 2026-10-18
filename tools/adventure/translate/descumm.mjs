// Parser de la salida de **descumm** (texto SCUMM V5) a un **AST JSON** (`adv-ast/1`).
// Objetivo I1 del pipeline (ROADMAP_ADVENTURE_IMPORT.md): convertir los guiones descompilados
// en una estructura que el traductor a C++ (`eng::adv`) pueda consumir sin volver a mirar texto.
//
// Gramática observada en el corpus real (MI1/MI2, 39 953 + 62 682 opcodes):
//   [addr] (op) if (<expr>) {          → abre bloque
//   [addr] (op) } else {               → cierra y abre rama else
//   [addr] (op) <lvalue> <op=> <expr>; → asignación (Exprmode opcional delante)
//   [addr] (op) goto <hex>;            → salto relativo (flujo del original)
//   [addr] (op) <call>(<args>);        → llamada a primitiva de aventura
//   [addr] (op) breakHere(); …         → marcadores de cesión/fin
//   [addr] (**) }                      → cierre sin opcode
// Expresiones: números, cadenas, VAR_*/nombres, Local[n]/Var[n]/Bit[n], arrays [a,b,…],
// llamadas anidadas (Init(), Costume(75)…), operadores aritméticos/lógicos, y asignaciones
// inline `(<VAR_RESULT = f()> / 2)`. Lo no reconocido queda como `kind:"raw"` (cobertura).

const ASSIGN_OPS = new Set(['=', '+=', '-=', '*=', '/=', '&=', '|=', '^=']);
const BIN_PREC = [
	['||'],
	['&&'],
	['|'],
	['^'],
	['&'],
	['==', '!='],
	['<=', '>=', '<', '>'],
	['<<', '>>'],
	['+', '-'],
	['*', '/', '%'],
];

/** Tokeniza una expresión/statement de descumm. Lanza en caracteres no reconocidos. */
function tokenize(text) {
	const toks = [];
	let i = 0;
	while (i < text.length) {
		const c = text[i];
		if (/\s/.test(c)) {
			i++;
			continue;
		}
		if (c === '"') {
			let s = '';
			i++;
			while (i < text.length && text[i] !== '"') {
				if (text[i] === '\\' && i + 1 < text.length) {
					// descumm escapa los bytes no imprimibles como `\xNN` (p. ej. `\xA0`); se
					// decodifica al carácter para que el guion generado conserve el byte real.
					const hex = /^x[0-9A-Fa-f]{2}/.exec(text.slice(i + 1));
					if (hex) {
						s += String.fromCharCode(parseInt(hex[0].slice(1), 16));
						i += 4;
					} else {
						s += text[++i];
						i++;
					}
				} else {
					s += text[i];
					i++;
				}
			}
			if (text[i] !== '"') throw new Error('cadena sin cerrar');
			i++;
			toks.push({ t: 'str', v: s });
			continue;
		}
		if (/[0-9]/.test(c)) {
			let j = i;
			while (j < text.length && /[0-9A-Fa-fxX]/.test(text[j])) j++;
			const raw = text.slice(i, j);
			toks.push({ t: 'num', v: raw.startsWith('0x') || raw.startsWith('0X') ? parseInt(raw, 16) : parseInt(raw, 10), raw });
			i = j;
			continue;
		}
		if (/[A-Za-z_]/.test(c)) {
			let j = i;
			while (j < text.length && /[A-Za-z0-9_.]/.test(text[j])) j++;
			toks.push({ t: 'id', v: text.slice(i, j) });
			i = j;
			continue;
		}
		const two = text.slice(i, i + 2);
		if (['==', '!=', '<=', '>=', '&&', '||', '<<', '>>', '+=', '-=', '*=', '/=', '&=', '|=', '^='].includes(two)) {
			toks.push({ t: 'op', v: two });
			i += 2;
			continue;
		}
		if ('()[]{},;<>+-*/%&|^!~='.includes(c)) {
			toks.push({ t: 'op', v: c });
			i++;
			continue;
		}
		throw new Error(`token no reconocido: ${JSON.stringify(c)}`);
	}
	return toks;
}

/** Parser de expresiones por precedencia; devuelve [nodo, tokens restantes]. */
function parseExpr(toks, minPrec = 0) {
	let [node, rest] = parseUnary(toks);
	while (rest.length > 0 && rest[0].t === 'op') {
		const op = rest[0].v;
		let prec = -1;
		for (let p = 0; p < BIN_PREC.length; p++) if (BIN_PREC[p].includes(op)) prec = p;
		if (prec < minPrec) break;
		const [rhs, r2] = parseExpr(rest.slice(1), prec + 1);
		node = { k: 'bin', op, a: node, b: rhs };
		rest = r2;
	}
	return [node, rest];
}

function parseUnary(toks) {
	if (toks.length === 0) throw new Error('expresión vacía');
	const t = toks[0];
	if (t.t === 'op' && (t.v === '!' || t.v === '-' || t.v === '~')) {
		const [a, rest] = parseUnary(toks.slice(1));
		return [{ k: 'un', op: t.v, a }, rest];
	}
	if (t.t === 'op' && t.v === '<') {
		// Asignación inline del original: (<VAR_RESULT = f()> / 2). El '>' de cierre se
		// localiza con profundidad de paréntesis/corchetes (si no, se confundiría con '>').
		const rest0 = toks.slice(1);
		let depth = 0;
		let end = -1;
		for (let i = 0; i < rest0.length; i++) {
			const v = rest0[i].v;
			if (v === '(' || v === '[') depth++;
			else if (v === ')' || v === ']') depth--;
			else if (v === '>' && depth === 0) {
				end = i;
				break;
			}
		}
		if (end < 0) throw new Error('falta > de asignación inline');
		const [stmt] = parseAssignLike(rest0.slice(0, end));
		return [{ k: 'inline', stmt }, rest0.slice(end + 1)];
	}
	return parsePrimary(toks);
}

function parsePrimary(toks) {
	const t = toks[0];
	if (!t) throw new Error('fin inesperado');
	if (t.t === 'num') return [{ k: 'num', v: t.v }, toks.slice(1)];
	if (t.t === 'str') return [{ k: 'str', v: t.v }, toks.slice(1)];
	if (t.t === 'id') {
		// Índice: Local[n], Var[n], Bit[n]
		if (toks[1]?.v === '[') {
			const [idx, r1] = parseExpr(toks.slice(2));
			if (r1[0]?.v !== ']') throw new Error('falta ] de índice');
			const space = /^(Local|Var|Bit)$/i.test(t.v) ? t.v.toLowerCase() : t.v;
			return [{ k: 'var', space, index: idx }, r1.slice(1)];
		}
		// Llamada: nombre(args)
		if (toks[1]?.v === '(') {
			const args = [];
			let rest = toks.slice(2);
			if (rest[0]?.v !== ')') {
				for (;;) {
					const [a, r] = parseExpr(rest);
					args.push(a);
					rest = r;
					if (rest[0]?.v === ',') {
						rest = rest.slice(1);
						if (rest[0]?.v === ')') break; // coma final de descumm (PutCodeInString(39,))
						continue;
					}
					break;
				}
			}
			if (rest[0]?.v !== ')') throw new Error(`falta ) en llamada a ${t.v}`);
			return [{ k: 'call', name: t.v, args }, rest.slice(1)];
		}
		return [{ k: 'name', v: t.v }, toks.slice(1)];
	}
	if (t.v === '(') {
		const [node, rest] = parseExpr(toks.slice(1));
		if (rest[0]?.v !== ')') throw new Error('falta )');
		return [node, rest.slice(1)];
	}
	if (t.v === '[') {
		const items = [];
		let rest = toks.slice(1);
		if (rest[0]?.v !== ']') {
			for (;;) {
				const [a, r] = parseExpr(rest);
				items.push(a);
				rest = r;
				if (rest[0]?.v === ',') {
					rest = rest.slice(1);
					continue;
				}
				break;
			}
		}
		if (rest[0]?.v !== ']') throw new Error('falta ]');
		return [{ k: 'arr', items }, rest.slice(1)];
	}
	throw new Error(`primario inesperado: ${JSON.stringify(t.v ?? t.t)}`);
}

/** Asignación `lvalue op expr` (sin `;`). Devuelve [stmt, rest]. `aop` es el operador de
 * asignación; el byte de opcode de la instrucción se añade después como `op` (no colisionan). */
function parseAssignLike(toks) {
	const [target, r1] = parseExpr(toks);
	if (!r1.length || r1[0].t !== 'op' || !ASSIGN_OPS.has(r1[0].v)) throw new Error('no es asignación');
	const [value, r2] = parseExpr(r1.slice(1));
	return [{ kind: 'assign', target, aop: r1[0].v, value }, r2];
}

/**
 * Parsea un cuerpo de línea (sin `[addr] (op)`), decidiendo el tipo de sentencia.
 * \param body texto de la instrucción.
 * \return sentencia AST (sin addr/op; los añade `parseDescummText`).
 */
function parseBody(body) {
	let text = body.trim();
	let exprmode = false;
	if (/^Exprmode\s+/.test(text)) {
		exprmode = true;
		text = text.replace(/^Exprmode\s+/, '');
	}
	// Comentarios que emite descumm (p. ej. `/* goto 001E; */`): se conservan como anotación.
	if (/^\/\*[\s\S]*\*\/;?$/.test(text)) {
		return { kind: 'comment', text: text.replace(/^\/\*|\*\/;?$/g, '').trim() };
	}
	if (/^goto\s+[0-9A-Fa-f]+;?$/.test(text)) {
		return { kind: 'goto', target: parseInt(text.replace(/^goto\s+/, '').replace(';', ''), 16) };
	}
	// `unless (cond) goto addr;` = salto si NO se cumple la condición (forma de descumm).
	{
		const m = /^unless\s*\(([\s\S]*)\)\s*goto\s+([0-9A-Fa-f]+);?$/.exec(text);
		if (m) {
			const [cond, rest] = parseExpr(tokenize(m[1]));
			if (rest.length) throw new Error('unless: condición con tokens sobrantes');
			return { kind: 'unless-goto', cond, target: parseInt(m[2], 16) };
		}
	}
	// Post-incremento/decremento como sentencia propia: `Var[100]++;`.
	{
		const m = /^(.+?)\s*(\+\+|--)\s*;?$/.exec(text);
		if (m && !/[;{}]/.test(m[1])) {
			const [target, rest] = parseExpr(tokenize(m[1]));
			if (!rest.length) return { kind: 'incdec', target, aop: m[2] };
		}
	}
	if (/^if\s*\(/.test(text)) {
		const m = /^if\s*\(([\s\S]*)\)\s*\{?$/.exec(text);
		if (!m) throw new Error('if sin condición');
		const [cond, rest] = parseExpr(tokenize(m[1]));
		if (rest.length) throw new Error('condición con tokens sobrantes');
		return { kind: 'if', cond, then: [] };
	}
	// Llamada o asignación con `;` final opcional.
	const toks = tokenize(text);
	if (!toks.length) return { kind: 'raw', text: body };
	let stmt;
	let rest;
	const isAssign = (() => {
		// Busca un operador de asignación a profundidad 0.
		let depth = 0;
		for (let i = 0; i < toks.length; i++) {
			if (toks[i].v === '(' || toks[i].v === '[') depth++;
			else if (toks[i].v === ')' || toks[i].v === ']') depth--;
			else if (depth === 0 && toks[i].t === 'op' && ASSIGN_OPS.has(toks[i].v)) return true;
		}
		return false;
	})();
	if (isAssign) {
		[stmt, rest] = parseAssignLike(toks);
		if (exprmode) stmt.exprmode = true;
	} else {
		const [expr, r] = parseExpr(toks);
		rest = r;
		stmt = expr.k === 'call' ? { kind: 'call', name: expr.name, args: expr.args } : { kind: 'expr', expr };
	}
	if (rest.length && rest[0].v !== ';') throw new Error('tokens sobrantes');
	return stmt;
}

/**
 * Parsea el texto completo de un .txt de descumm a AST jerárquico.
 * \param text contenido del descompilado (CRLF o LF).
 * \param meta { script, room } para la cabecera del resultado.
 * \return { format:'adv-ast/1', script, room, statements, stats }.
 */
export function parseDescummText(text, meta = {}) {
	const lines = String(text).replace(/\r/g, '').split('\n');
	const root = [];
	const stack = [{ list: root }];
	const stats = { lines: 0, parsed: 0, raw: 0, ifs: 0, gotos: 0 };
	for (const line of lines) {
		const m = /^\[([0-9A-Fa-f]+)\] \(([0-9A-Fa-f*]{2})\)\s?(.*)$/.exec(line);
		if (!m) continue;
		const addr = parseInt(m[1], 16);
		const op = m[2] === '**' ? null : parseInt(m[2], 16);
		const body = m[3].trim();
		if (!body) continue;
		stats.lines++;
		// Cierres de bloque y ramas.
		if (body === '}') {
			if (stack.length > 1) stack.pop();
			continue;
		}
		const elseMatch = /^\}\s*else\s*(\{?)\s*(.*)$/.exec(body);
		if (elseMatch || /^else\b/.test(body)) {
			const rest = elseMatch ? elseMatch[2] : body.replace(/^else\s*\{?\s*/, '');
			if (stack.length > 1) {
				const closed = stack.pop(); // rama `then` del if
				const ifStmt = stack[stack.length - 1].ifStmt ?? closed.ifStmt;
				const parent = stack[stack.length - 1];
				// `ifStmt` está en la lista del padre; abrimos su rama else.
				if (!ifStmt) {
					stats.raw++;
					continue;
				}
				ifStmt.else = ifStmt.else ?? [];
				if (rest && rest !== '{') {
					// `else if (...) {` u otra sentencia tras else.
					try {
						const inner = parseBody(rest);
						inner.addr = addr;
						inner.op = op;
						ifStmt.else.push(inner);
						if (inner.kind === 'if') stack.push({ list: inner.then, ifStmt: inner });
					} catch {
						stats.raw++;
					}
				}
				stack.push({ list: ifStmt.else, ifStmt });
				stats.parsed++;
			}
			continue;
		}
		let stmt;
		try {
			stmt = parseBody(body);
		} catch (err) {
			// Se conserva el motivo para que la cobertura sea accionable (no se oculta).
			stmt = { kind: 'raw', text: body, error: err.message };
		}
		stmt.addr = addr;
		stmt.op = op;
		if (stmt.kind === 'raw') stats.raw++;
		else stats.parsed++;
		if (stmt.kind === 'if') {
			stats.ifs++;
			stack[stack.length - 1].list.push(stmt);
			stack.push({ list: stmt.then, ifStmt: stmt });
		} else {
			if (stmt.kind === 'goto') stats.gotos++;
			stack[stack.length - 1].list.push(stmt);
		}
	}
	return { format: 'adv-ast/1', script: meta.script ?? '', room: meta.room ?? 0, statements: root, stats };
}
