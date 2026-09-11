#!/usr/bin/env node
/**
 * UAF-R packer — exportador host del contenedor `eng::assets::Blob`
 * (`engine/include/eng/assets/uaf.hpp`).
 *
 * Adapta el concepto del "cooked asset" del proyecto UAF (authoring `.uaf` →
 * runtime) al formato **UAF-R** que consume el engine Amiga: un contenedor de
 * chunks big-endian validado por offsets, sin parsing pesado en el Amiga.
 *
 * Formato (idéntico al del runtime C++):
 *
 *   header  { u32 magic="UAFR", u16 version, u16 chunk_count }         (BE)
 *   chunk[] { u16 type, u16 count, u32 size, <size bytes>, pad a 4 }   (BE)
 *
 * Incluye el paso cercano al core Amiga: **chunky indexado → bitplanes** separados
 * (lo que en la CLI UAF llamarían `amiga convert`), y el empaquetado de paleta y
 * sample. Todo es puro y host-testable (ver `test-uaf-pack.ts`).
 *
 * Uso:
 *   node dist/tools/assets/uaf-pack.js <out.uafr>
 */
import * as fs from 'fs';
import { pathToFileURL } from 'url';
export const UAF_MAGIC = 0x55414652; // "UAFR"
export const UAF_VERSION = 1;
/** Tipos de chunk (deben coincidir con `eng::assets::ChunkType`). */
export var UafChunkType;
(function (UafChunkType) {
    UafChunkType[UafChunkType["Palette"] = 1] = "Palette";
    UafChunkType[UafChunkType["Bitplanes"] = 2] = "Bitplanes";
    UafChunkType[UafChunkType["CopperTemplates"] = 3] = "CopperTemplates";
    UafChunkType[UafChunkType["PatchTables"] = 4] = "PatchTables";
    UafChunkType[UafChunkType["Sprites"] = 5] = "Sprites";
    UafChunkType[UafChunkType["Bobs"] = 6] = "Bobs";
    UafChunkType[UafChunkType["Tiles"] = 7] = "Tiles";
    UafChunkType[UafChunkType["Collision"] = 8] = "Collision";
    UafChunkType[UafChunkType["Strings"] = 9] = "Strings";
    UafChunkType[UafChunkType["Samples"] = 10] = "Samples";
    UafChunkType[UafChunkType["Modules"] = 11] = "Modules";
})(UafChunkType || (UafChunkType = {}));
/** Cabecera de bitplanes (igual que `eng::assets::BitplanesView`). */
export const BITPLANES_HEADER_BYTES = 10;
const pad4 = (n) => (n + 3) & ~3;
/**
 * Empaqueta chunks en un blob UAF-R (big-endian, chunks 4-alineados).
 */
export function packUaf(chunks) {
    let total = 8;
    for (const c of chunks) {
        total += pad4(8 + c.data.length);
    }
    const buf = Buffer.alloc(total);
    buf.writeUInt32BE(UAF_MAGIC, 0);
    buf.writeUInt16BE(UAF_VERSION, 4);
    buf.writeUInt16BE(chunks.length, 6);
    let off = 8;
    for (const c of chunks) {
        buf.writeUInt16BE(c.type, off);
        buf.writeUInt16BE(c.count, off + 2);
        buf.writeUInt32BE(c.data.length, off + 4);
        Buffer.from(c.data).copy(buf, off + 8);
        off += pad4(8 + c.data.length);
    }
    return buf;
}
/**
 * Parsea y valida un blob UAF-R. Lanza si el header o algún chunk se sale.
 */
export function parseUaf(buf) {
    if (buf.length < 8) {
        throw new Error('blob demasiado corto');
    }
    if (buf.readUInt32BE(0) !== UAF_MAGIC) {
        throw new Error('magic inválido (se esperaba UAFR)');
    }
    if (buf.readUInt16BE(4) !== UAF_VERSION) {
        throw new Error(`versión no soportada: ${buf.readUInt16BE(4)}`);
    }
    const n = buf.readUInt16BE(6);
    const out = [];
    let off = 8;
    for (let i = 0; i < n; i++) {
        if (off + 8 > buf.length) {
            throw new Error('cabecera de chunk fuera del blob');
        }
        const type = buf.readUInt16BE(off);
        const count = buf.readUInt16BE(off + 2);
        const size = buf.readUInt32BE(off + 4);
        off += 8;
        if (off + size > buf.length) {
            throw new Error('datos de chunk fuera del blob');
        }
        out.push({ type, count, offset: off, size });
        off += pad4(size);
    }
    return out;
}
/**
 * Convierte una imagen **chunky indexada** (1 byte/píxel, índice 0..2^planes-1)
 * a **bitplanes separados**: plano 0 completo, luego plano 1, etc.; `row_bytes`
 * = width/8. Es el paso "amiga convert" del core (CPU, determinista).
 */
export function bitplanesFromIndexed(width, height, planes, pixels) {
    if (width % 8 !== 0 || width === 0 || height === 0 || planes === 0 || planes > 8) {
        throw new Error('geometría inválida (width múltiplo de 8, 1..8 planos)');
    }
    if (pixels.length < width * height) {
        throw new Error('pixels insuficientes');
    }
    const rowBytes = width / 8;
    const out = new Uint8Array(rowBytes * height * planes);
    for (let p = 0; p < planes; p++) {
        const planeBase = p * rowBytes * height;
        const bit = 1 << p;
        for (let y = 0; y < height; y++) {
            for (let xb = 0; xb < rowBytes; xb++) {
                let acc = 0;
                for (let b = 0; b < 8; b++) {
                    if (pixels[y * width + xb * 8 + b] & bit) {
                        acc |= 1 << (7 - b);
                    }
                }
                out[planeBase + y * rowBytes + xb] = acc;
            }
        }
    }
    return out;
}
/** Datos del chunk de paleta (N colores RGB444 en u16 big-endian). */
export function paletteChunkData(colors) {
    const out = new Uint8Array(colors.length * 2);
    for (let i = 0; i < colors.length; i++) {
        out[i * 2] = (colors[i] >> 8) & 0xff;
        out[i * 2 + 1] = colors[i] & 0xff;
    }
    return out;
}
/** Datos del chunk de bitplanes: cabecera de geometría + planos separados. */
export function bitplanesChunkData(width, height, planes, layout, planar) {
    const out = new Uint8Array(BITPLANES_HEADER_BYTES + planar.length);
    // width, height, row_bytes, planes, layout, flags, resv
    out[0] = (width >> 8) & 0xff;
    out[1] = width & 0xff;
    out[2] = (height >> 8) & 0xff;
    out[3] = height & 0xff;
    const rowBytes = width / 8;
    out[4] = (rowBytes >> 8) & 0xff;
    out[5] = rowBytes & 0xff;
    out[6] = planes & 0xff;
    out[7] = layout & 0xff;
    out[8] = 0;
    out[9] = 0;
    out.set(planar, BITPLANES_HEADER_BYTES);
    return out;
}
/** Datos del chunk de sample (bytes tal cual). */
export function sampleChunkData(bytes) {
    return bytes;
}
/**
 * Datos del chunk de textos: cadenas UTF-8 separadas por NUL (cada una con su
 * terminador). Es el formato que consume `eng::assets::StringsView` (C++).
 */
export function stringsChunkData(strings) {
    const parts = strings.map((s) => Buffer.from(s, 'utf8'));
    const out = Buffer.alloc(parts.reduce((n, p) => n + p.length + 1, 0));
    let o = 0;
    for (const p of parts) {
        p.copy(out, o);
        o += p.length + 1; // el byte NUL queda a 0 (Buffer.alloc)
    }
    return out;
}
/**
 * Datos del chunk de tiles: concatena tiles de tamaño fijo `tileBytes` (por
 * defecto, el del primer tile). Consumido por `eng::assets::TilesView` (C++).
 */
export function tilesChunkData(tiles, tileBytes) {
    if (tiles.length === 0) {
        return new Uint8Array(0);
    }
    const t = tileBytes ?? tiles[0].length;
    const out = Buffer.alloc(tiles.length * t);
    tiles.forEach((tile, i) => {
        if (tile.length !== t) {
            throw new Error(`tile ${i}: ${tile.length} bytes != ${t}`);
        }
        Buffer.from(tile).copy(out, i * t);
    });
    return out;
}
function buildDemo() {
    // Paleta de 4 colores (negro, gris, rojo, blanco) RGB444.
    const palette = [0x000, 0x888, 0xf00, 0xfff];
    // Imagen 16x16 indexada (0..3) en damero + barra.
    const w = 16, h = 16;
    const px = new Uint8Array(w * h);
    for (let y = 0; y < h; y++) {
        for (let x = 0; x < w; x++) {
            px[y * w + x] = (x < 4) ? 3 : ((x ^ y) & 2 ? 1 : 0);
        }
    }
    const planar = bitplanesFromIndexed(w, h, 2, px);
    // Sample: 16 bytes (s8) de rampa.
    const sample = new Uint8Array(16);
    for (let i = 0; i < 16; i++)
        sample[i] = (i * 8) & 0xff;
    return packUaf([
        { type: UafChunkType.Palette, count: palette.length, data: paletteChunkData(palette) },
        { type: UafChunkType.Bitplanes, count: 1, data: bitplanesChunkData(w, h, 2, 0, planar) },
        { type: UafChunkType.Samples, count: 1, data: sampleChunkData(sample) },
    ]);
}
function main() {
    const out = process.argv[2];
    if (!out) {
        console.error('Uso: uaf-pack.js <out.uafr>');
        process.exit(2);
    }
    const blob = buildDemo();
    const chunks = parseUaf(blob); // auto-validación: el contenedor debe re-parsearse
    fs.writeFileSync(out, blob);
    console.log(`OK uaf-pack: ${blob.length} bytes -> ${out} (${chunks.length} chunks)`);
}
if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
    main();
}
