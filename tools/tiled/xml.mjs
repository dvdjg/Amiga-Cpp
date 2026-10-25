#!/usr/bin/env node
// Parser XML mínimo y estructural para documentos de Tiled (.tmx/.tsx).
//
// Tiled usa un subconjunto de XML sin namespaces: declaración, comentarios, CDATA,
// elementos anidados, atributos entre comillas y las cinco entidades predefinidas
// más referencias numéricas. Este parser cubre ese subconjunto, conserva el orden de
// los hijos y las posiciones (línea/columna) para diagnósticos accionables. No es un
// parser XML general: rechaza lo que no entiende en vez de aceptarlo en silencio.
//
// Uso interno desde `parse-tmx.mjs` y los tests de `tools/tiled/`.

export class XmlSyntaxError extends Error {
	constructor(message, line, column, source) {
		super(`${source}:${line}:${column}: ${message}`);
		this.name = 'XmlSyntaxError';
		this.line = line;
		this.column = column;
		this.source = source;
	}
}

const NAME_START = /[A-Za-z_:]/;
const NAME_CHAR = /[A-Za-z0-9_:.\-]/;
const PREDEFINED = new Map([
	['amp', '&'],
	['lt', '<'],
	['gt', '>'],
	['quot', '"'],
	['apos', "'"],
]);

function decodeEntities(value, fail) {
	let out = '';
	let at = 0;
	while (at < value.length) {
		const amp = value.indexOf('&', at);
		if (amp < 0) {
			out += value.slice(at);
			break;
		}
		out += value.slice(at, amp);
		const semi = value.indexOf(';', amp + 1);
		if (semi < 0) fail('entidad sin ";" de cierre');
		const body = value.slice(amp + 1, semi);
		if (body.startsWith('#x') || body.startsWith('#X')) {
			const code = Number.parseInt(body.slice(2), 16);
			if (!Number.isFinite(code) || code < 0 || code > 0x10ffff) fail(`referencia numérica inválida "&${body};"`);
			out += String.fromCodePoint(code);
		} else if (body.startsWith('#')) {
			const code = Number.parseInt(body.slice(1), 10);
			if (!Number.isFinite(code) || code < 0 || code > 0x10ffff) fail(`referencia numérica inválida "&${body};"`);
			out += String.fromCodePoint(code);
		} else if (PREDEFINED.has(body)) {
			out += PREDEFINED.get(body);
		} else {
			fail(`entidad desconocida "&${body};"`);
		}
		at = semi + 1;
	}
	return out;
}

/// Parsea un documento XML y devuelve el nodo raíz.
/// Nodo: `{ name, attrs, children, text, line, column }`. `text` es el texto directo
/// del elemento (sin el de sus hijos), ya decodificado.
export function parseXml(text, { source = '<xml>' } = {}) {
	let i = 0;
	let line = 1;
	let column = 1;
	const length = text.length;

	const fail = (message, atLine = line, atColumn = column) => {
		throw new XmlSyntaxError(message, atLine, atColumn, source);
	};
	const bump = (count = 1) => {
		for (let k = 0; k < count; ++k) {
			if (text[i] === '\n') {
				++line;
				column = 1;
			} else {
				++column;
			}
			++i;
		}
	};
	const startsWith = (token) => text.startsWith(token, i);
	const skipWhitespace = () => {
		while (i < length && /\s/.test(text[i])) bump();
	};
	const parseName = () => {
		if (i >= length || !NAME_START.test(text[i])) fail('se esperaba un nombre de etiqueta');
		let name = '';
		while (i < length && NAME_CHAR.test(text[i])) {
			name += text[i];
			bump();
		}
		return name;
	};
	const skipUntil = (token, what) => {
		const end = text.indexOf(token, i);
		if (end < 0) fail(`${what} sin cerrar`);
		bump(end - i + token.length);
	};
	const skipComment = () => {
		bump(4); // <!--
		skipUntil('-->', 'comentario');
	};
	const skipCdata = () => {
		bump(9); // <![CDATA[
		const end = text.indexOf(']]>', i);
		if (end < 0) fail('CDATA sin cerrar');
		const raw = text.slice(i, end);
		bump(end - i + 3);
		return raw;
	};
	const skipProcessing = () => {
		bump(2); // <?
		skipUntil('?>', 'instrucción de procesamiento');
	};
	const skipDoctype = () => {
		// DOCTYPE puede tener un subconjunto interno con corchetes; se salta equilibrando
		// corchetes y respetando comillas para no cortar en el primer `>`.
		bump(9); // <!DOCTYPE
		let depth = 0;
		let quote = '';
		while (i < length) {
			const ch = text[i];
			if (quote !== '') {
				if (ch === quote) quote = '';
			} else if (ch === '"' || ch === "'") {
				quote = ch;
			} else if (ch === '[') {
				++depth;
			} else if (ch === ']') {
				if (depth > 0) --depth;
			} else if (ch === '>' && depth === 0) {
				bump();
				return;
			}
			bump();
		}
		fail('DOCTYPE sin cerrar');
	};
	const parseAttributes = () => {
		const attrs = {};
		skipWhitespace();
		while (i < length && text[i] !== '/' && text[i] !== '>') {
			const name = parseName();
			if (Object.prototype.hasOwnProperty.call(attrs, name)) fail(`atributo duplicado "${name}"`);
			skipWhitespace();
			if (text[i] !== '=') fail(`atributo "${name}" sin "="`);
			bump();
			skipWhitespace();
			const quote = text[i];
			if (quote !== '"' && quote !== "'") fail(`atributo "${name}" sin comillas`);
			bump();
			let raw = '';
			while (i < length && text[i] !== quote) {
				raw += text[i];
				bump();
			}
			if (i >= length) fail(`atributo "${name}" sin cerrar`);
			bump();
			attrs[name] = decodeEntities(raw, fail);
			skipWhitespace();
		}
		return attrs;
	};
	const parseElement = () => {
		if (text[i] !== '<') fail('se esperaba "<"');
		const startLine = line;
		const startColumn = column;
		bump(); // <
		const name = parseName();
		const attrs = parseAttributes();
		skipWhitespace();
		if (text[i] === '/') {
			bump();
			if (text[i] !== '>') fail(`cierre inválido en <${name}>`);
			bump();
			return { name, attrs, children: [], text: '', line: startLine, column: startColumn };
		}
		if (text[i] !== '>') fail(`se esperaba ">" en <${name}>`);
		bump();
		const node = { name, attrs, children: [], text: '', line: startLine, column: startColumn };
		while (i < length) {
			if (startsWith('</')) {
				bump(2);
				const close = parseName();
				skipWhitespace();
				if (text[i] !== '>') fail(`cierre inválido en </${close}>`);
				bump();
				if (close !== name) fail(`cierre </${close}> no coincide con <${name}>`);
				return node;
			}
			if (startsWith('<!--')) {
				skipComment();
				continue;
			}
			if (startsWith('<![CDATA[')) {
				node.text += skipCdata();
				continue;
			}
			if (startsWith('<?')) {
				skipProcessing();
				continue;
			}
			if (startsWith('<!DOCTYPE')) {
				skipDoctype();
				continue;
			}
			if (text[i] === '<') {
				node.children.push(parseElement());
				continue;
			}
			let raw = '';
			while (i < length && text[i] !== '<') {
				raw += text[i];
				bump();
			}
			node.text += decodeEntities(raw, fail);
		}
		fail(`<${name}> sin cerrar`, startLine, startColumn);
	};

	if (text.charCodeAt(0) === 0xfeff) bump();
	skipWhitespace();
	while (startsWith('<?') || startsWith('<!--') || startsWith('<!DOCTYPE')) {
		if (startsWith('<?')) skipProcessing();
		else if (startsWith('<!--')) skipComment();
		else skipDoctype();
		skipWhitespace();
	}
	if (i >= length || text[i] !== '<') fail('se esperaba un elemento raíz');
	const root = parseElement();
	skipWhitespace();
	if (i < length) fail('contenido después del elemento raíz');
	return root;
}

export function child(node, name) {
	return node?.children.find((item) => item.name === name) ?? null;
}

export function childrenNamed(node, names) {
	const wanted = new Set(names);
	return node?.children.filter((item) => wanted.has(item.name)) ?? [];
}

export function hasAttr(node, name) {
	return node != null && Object.prototype.hasOwnProperty.call(node.attrs, name);
}
