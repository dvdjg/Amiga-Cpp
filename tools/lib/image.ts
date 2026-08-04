/**
 * Utilidades de imagen compartidas por las herramientas TypeScript.
 *
 * Sustituyen a Pillow (PIL) y numpy de los antiguos analizadores en Python con
 * una implementacion 100% Node basada en `pngjs` (decodificador/encodificador
 * PNG puro en JS, sin binarios nativos). De este modo el ecosistema de scripting
 * queda unificado en Node/TypeScript y funciona en Windows, Linux y macOS.
 */
import * as fs from 'fs';
import { PNG } from 'pngjs';

/** Imagen RGBA en memoria: filas de arriba a abajo, 4 bytes por pixel. */
export interface RgbaImage {
	width: number;
	height: number;
	data: Uint8Array;
}

/** Devuelve el color [r,g,b] de un pixel de la imagen. */
export function pixel(image: RgbaImage, x: number, y: number): [number, number, number] {
	const i = (y * image.width + x) * 4;
	return [image.data[i], image.data[i + 1], image.data[i + 2]];
}

/** Escribe el color [r,g,b,a] de un pixel de la imagen. */
export function setPixel(image: RgbaImage, x: number, y: number, r: number, g: number, b: number, a = 255): void {
	const i = (y * image.width + x) * 4;
	image.data[i] = r;
	image.data[i + 1] = g;
	image.data[i + 2] = b;
	image.data[i + 3] = a;
}

/** Lee un PNG del disco y devuelve su contenido RGBA. */
export function readPng(path: string): RgbaImage {
	const png = PNG.sync.read(fs.readFileSync(path));
	return {
		width: png.width,
		height: png.height,
		data: new Uint8Array(png.data.buffer, png.data.byteOffset, png.data.byteLength),
	};
}

/** Crea una imagen RGBA en memoria con un color de fondo opcional. */
export function createImage(width: number, height: number, fill: [number, number, number] = [0, 0, 0]): RgbaImage {
	const data = new Uint8Array(width * height * 4);
	const image: RgbaImage = { width, height, data };
	if (fill[0] !== 0 || fill[1] !== 0 || fill[2] !== 0) {
		for (let i = 0; i < width * height; ++i) {
			data[i * 4] = fill[0];
			data[i * 4 + 1] = fill[1];
			data[i * 4 + 2] = fill[2];
			data[i * 4 + 3] = 255;
		}
	}
	return image;
}

/** Guarda una imagen RGBA como PNG en el disco. */
export function savePng(image: RgbaImage, path: string): void {
	const png = new PNG({ width: image.width, height: image.height });
	Buffer.from(image.data.buffer, image.data.byteOffset, image.data.byteLength).copy(png.data);
	fs.mkdirSync(require('path').dirname(path), { recursive: true });
	fs.writeFileSync(path, PNG.sync.write(png));
}

/** Copia una region rectangular de un origen a un destino. */
export function copyRect(src: RgbaImage, dst: RgbaImage, sx: number, sy: number, dx: number, dy: number, w: number, h: number): void {
	for (let y = 0; y < h; ++y) {
		const si = ((sy + y) * src.width + sx) * 4;
		const di = ((dy + y) * dst.width + dx) * 4;
		dst.data.set(src.data.subarray(si, si + w * 4), di);
	}
}

// ---------------------------------------------------------------------------
// Mini fuente de mapa de bits 5x7 para las hojas de contacto. Renderiza los
// caracteres ASCII mas comunes sin depender de fuentes del sistema.
// ---------------------------------------------------------------------------
const FONT: Record<string, string[]> = {
	' ': ['00000', '00000', '00000', '00000', '00000', '00000', '00000'],
	'.': ['00000', '00000', '00000', '00000', '00000', '01100', '01100'],
	',': ['00000', '00000', '00000', '00000', '01100', '00110', '00110'],
	'-': ['00000', '00000', '00000', '01110', '00000', '00000', '00000'],
	'_': ['00000', '00000', '00000', '00000', '00000', '00000', '11111'],
	'/': ['00001', '00010', '00010', '00100', '01000', '01000', '10000'],
	'(': ['00100', '01000', '10000', '10000', '10000', '01000', '00100'],
	')': ['00100', '00010', '00001', '00001', '00001', '00010', '00100'],
	':': ['00000', '01100', '01100', '00000', '01100', '01100', '00000'],
	'=': ['00000', '00000', '11111', '00000', '11111', '00000', '00000'],
	'>': ['10000', '01000', '00100', '00010', '00100', '01000', '10000'],
	'<': ['00001', '00010', '00100', '01000', '00100', '00010', '00001'],
	'0': ['01110', '10001', '10011', '10101', '11001', '10001', '01110'],
	'1': ['00100', '01100', '00100', '00100', '00100', '00100', '01110'],
	'2': ['01110', '10001', '00001', '00010', '00100', '01000', '11111'],
	'3': ['11110', '00001', '00001', '01110', '00001', '00001', '11110'],
	'4': ['00010', '00110', '01010', '10010', '11111', '00010', '00010'],
	'5': ['11111', '10000', '11110', '00001', '00001', '00001', '11110'],
	'6': ['01110', '10000', '10000', '11110', '10001', '10001', '01110'],
	'7': ['11111', '00001', '00010', '00100', '01000', '01000', '01000'],
	'8': ['01110', '10001', '10001', '01110', '10001', '10001', '01110'],
	'9': ['01110', '10001', '10001', '01111', '00001', '00001', '01110'],
	'A': ['01110', '10001', '10001', '11111', '10001', '10001', '10001'],
	'B': ['11110', '10001', '10001', '11110', '10001', '10001', '11110'],
	'C': ['01110', '10001', '10000', '10000', '10000', '10001', '01110'],
	'D': ['11100', '10010', '10001', '10001', '10001', '10010', '11100'],
	'E': ['11111', '10000', '10000', '11110', '10000', '10000', '11111'],
	'F': ['11111', '10000', '10000', '11110', '10000', '10000', '10000'],
	'G': ['01110', '10001', '10000', '10111', '10001', '10001', '01111'],
	'H': ['10001', '10001', '10001', '11111', '10001', '10001', '10001'],
	'I': ['01110', '00100', '00100', '00100', '00100', '00100', '01110'],
	'J': ['00111', '00010', '00010', '00010', '00010', '10010', '01100'],
	'K': ['10001', '10010', '10100', '11000', '10100', '10010', '10001'],
	'L': ['10000', '10000', '10000', '10000', '10000', '10000', '11111'],
	'M': ['10001', '11011', '10101', '10101', '10001', '10001', '10001'],
	'N': ['10001', '11001', '10101', '10011', '10001', '10001', '10001'],
	'O': ['01110', '10001', '10001', '10001', '10001', '10001', '01110'],
	'P': ['11110', '10001', '10001', '11110', '10000', '10000', '10000'],
	'Q': ['01110', '10001', '10001', '10001', '10101', '10010', '01101'],
	'R': ['11110', '10001', '10001', '11110', '10100', '10010', '10001'],
	'S': ['01111', '10000', '10000', '01110', '00001', '00001', '11110'],
	'T': ['11111', '00100', '00100', '00100', '00100', '00100', '00100'],
	'U': ['10001', '10001', '10001', '10001', '10001', '10001', '01110'],
	'V': ['10001', '10001', '10001', '10001', '10001', '01010', '00100'],
	'W': ['10001', '10001', '10001', '10101', '10101', '10101', '01010'],
	'X': ['10001', '10001', '01010', '00100', '01010', '10001', '10001'],
	'Y': ['10001', '10001', '01010', '00100', '00100', '00100', '00100'],
	'Z': ['11111', '00001', '00010', '00100', '01000', '10000', '11111'],
};

/** Dibuja texto con la mini fuente 5x7 a partir de (x, y). Devuelve el ancho usado. */
export function drawText(image: RgbaImage, x: number, y: number, text: string, color: [number, number, number]): number {
	const upper = text.toUpperCase();
	let cursor = x;
	for (const ch of upper) {
		const glyph = FONT[ch] ?? FONT['?'];
		for (let gy = 0; gy < 7; ++gy) {
			const row = glyph[gy];
			for (let gx = 0; gx < 5; ++gx) {
				if (row[gx] === '1') {
					const px = cursor + gx;
					const py = y + gy;
					if (px >= 0 && px < image.width && py >= 0 && py < image.height) {
						setPixel(image, px, py, color[0], color[1], color[2]);
					}
				}
			}
		}
		cursor += 6;
	}
	return cursor - x;
}

/** Ancho aproximado de un texto con la mini fuente (6 px por caracter). */
export function textWidth(text: string): number {
	return text.length * 6;
}
