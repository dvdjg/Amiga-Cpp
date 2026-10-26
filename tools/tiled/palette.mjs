#!/usr/bin/env node
// Utilidades de paleta compartidas por los cookers de Tiled.
//
// `paletteColors` reconstruye la paleta visible del perfil (bases + half-brights
// EHB) y `nearest` cuantiza un RGB al índice más cercano. La usan
// `cook-animations.mjs` y `cook-objects.mjs` para que las celdas vivas y los
// objetos de primer plano compartan exactamente la misma decisión de color.

/// Colores RGB del perfil en orden de índice Amiga.
/// \param {object} palette `palette.json` del perfil (`bases` y opcionalmente `planes`).
/// \param {number} planes planos del perfil (1..6); con 6 se añaden los half-brights EHB.
/// \returns {number[][]} índice -> `[r,g,b]`; los huecos quedan sin definir.
export function paletteColors(palette, planes) {
	const bases = Array.isArray(palette?.bases) ? palette.bases : [];
	const baseCount = Math.min(bases.length, planes >= 6 ? 32 : 1 << planes);
	if (baseCount === 0) throw new Error('palette.json no contiene bases');
	const colors = [];
	for (let k = 0; k < baseCount; ++k) {
		const b = bases[k];
		colors[k] = [b[0] & 255, b[1] & 255, b[2] & 255];
		if (planes >= 6) colors[32 + k] = [b[0] >> 1, b[1] >> 1, b[2] >> 1];
	}
	return colors;
}

/// Índice de paleta más cercano a `(r,g,b)` por distancia euclídea al cuadrado.
/// \param {number[][]} colors tabla de `paletteColors`.
/// \returns {number} índice 0..colors.length-1.
export function nearest(colors, r, g, b) {
	let best = 0;
	let bestD = Infinity;
	for (let i = 0; i < colors.length; ++i) {
		const p = colors[i];
		if (p == null) continue;
		const dr = r - p[0];
		const dg = g - p[1];
		const db = b - p[2];
		const d = dr * dr + dg * dg + db * db;
		if (d < bestD) {
			bestD = d;
			best = i;
		}
	}
	return best;
}

/// Nivel 0..4 de cobertura de una sombra según su alfa de origen.
///
/// El arte de sombras de Tiled suele ser negro con alfa fraccional (p. ej. 64 =
/// 25 %). El modo `dither` aproxima esa cobertura con una matriz ordenada 2x2;
/// el alfa 255 se considera opaco (4/4) y los valores por debajo de 64 se
/// descartan para no ensuciar el fondo con motas aisladas.
/// \param {number} alpha alfa 0..255 del píxel fuente.
/// \returns {number} 0 = transparente; 4 = todos los píxeles del bloque 2x2.
export function shadowLevel(alpha) {
	if (alpha < 64) return 0;
	if (alpha < 128) return 1;
	if (alpha < 192) return 2;
	if (alpha < 255) return 3;
	return 4;
}

/// Matriz ordenada 2x2 (Bayer) usada por `shadowLevel`.
export const BAYER2 = Object.freeze([
	Object.freeze([0, 2]),
	Object.freeze([3, 1]),
]);

/// ¿El píxel `(x,y)` cae dentro de la cobertura `level` del bloque 2x2?
/// \param {number} x columna de la celda.
/// \param {number} y fila de la celda.
/// \param {number} level 0..4 de `shadowLevel`.
export function ditherCovers(x, y, level) {
	return level >= 4 || BAYER2[y & 1][x & 1] < level;
}
