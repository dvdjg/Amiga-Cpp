// ============================================================================
// sprite-pad: cocina de márgenes del FastCopy (F5)
// ============================================================================
//
// Calcula el **padding transparente** que necesita un BOB del camino
// `BobDraw::Opaque` + `FastBobLayer` (dual playfield fast BOBs) a partir del
// **desplazamiento máximo por eje** del objeto, y genera la hoja con padding.
// La fórmula es la MISMA que `engine/include/eng/graphics/fast_bob_margins.hpp`
// (HOST-437): `left/right/top/bottom` = pasos máximos por eje, doblados con doble
// buffer; `paddedW = w + left + right`; `paddedH = h + top + bottom`; el contenido
// queda en `(left, top)` (el juego sigue posicionando por la esquina lógica).
//
// Uso:
//   node dist/tools/assets/sprite-pad.js <in.png> --left N --right N --up N --down N
//        [--double-buffer] --out <outDir>
//
// Salidas (en `outDir`):
//   <base>.padded.png    hoja con el padding transparente
//   <base>.margins.json  { left, right, top, bottom, paddedW, paddedH }
//
// Referencias: `docs/reference/amiga/techniques/dual-playfield-fastbobs.md`,
// `docs/guides/roadmap/ROADMAP_SPRITES_COMPLETO.md` §3.4.
import * as fs from 'fs';
import * as path from 'path';
import { pathToFileURL } from 'url';
import { PNG } from 'pngjs';
/// Márgenes del FastCopy (paridad exacta con `fast_bob_margins` del engine).
export function fastBobMargins(w, h, maxLeft, maxRight, maxUp, maxDown, doubleBuffer = false) {
    const mul = doubleBuffer ? 2 : 1;
    const left = maxLeft * mul;
    const right = maxRight * mul;
    const top = maxUp * mul;
    const bottom = maxDown * mul;
    return { left, right, top, bottom, paddedW: w + left + right, paddedH: h + top + bottom };
}
/// Hoja con padding transparente: el contenido queda en `(left, top)`.
export function padSprite(src, m) {
    const out = new PNG({ width: m.paddedW, height: m.paddedH });
    out.data.fill(0); // transparente (RGBA = 0,0,0,0)
    for (let y = 0; y < src.height; y++) {
        for (let x = 0; x < src.width; x++) {
            const si = (src.width * y + x) << 2;
            const di = (m.paddedW * (y + m.top) + (x + m.left)) << 2;
            out.data[di] = src.data[si];
            out.data[di + 1] = src.data[si + 1];
            out.data[di + 2] = src.data[si + 2];
            out.data[di + 3] = src.data[si + 3];
        }
    }
    return out;
}
function parseArgs(argv) {
    const args = {
        input: '',
        left: 0,
        right: 0,
        up: 0,
        down: 0,
        doubleBuffer: false,
        outDir: '',
    };
    for (let i = 0; i < argv.length; i++) {
        const a = argv[i];
        if (a === '--left')
            args.left = parseInt(argv[++i] ?? '0', 10);
        else if (a === '--right')
            args.right = parseInt(argv[++i] ?? '0', 10);
        else if (a === '--up')
            args.up = parseInt(argv[++i] ?? '0', 10);
        else if (a === '--down')
            args.down = parseInt(argv[++i] ?? '0', 10);
        else if (a === '--double-buffer')
            args.doubleBuffer = true;
        else if (a === '--out')
            args.outDir = argv[++i] ?? '';
        else if (!a.startsWith('--'))
            args.input = a;
    }
    if (!args.input || !args.outDir)
        return null;
    return args;
}
function main() {
    const args = parseArgs(process.argv.slice(2));
    if (!args) {
        console.error('Uso: node dist/tools/assets/sprite-pad.js <in.png> --left N --right N --up N --down N [--double-buffer] --out <outDir>');
        process.exit(2);
    }
    const src = PNG.sync.read(fs.readFileSync(args.input));
    const m = fastBobMargins(src.width, src.height, args.left, args.right, args.up, args.down, args.doubleBuffer);
    const padded = padSprite(src, m);
    fs.mkdirSync(args.outDir, { recursive: true });
    const base = path.basename(args.input, path.extname(args.input));
    const pngPath = path.join(args.outDir, `${base}.padded.png`);
    const jsonPath = path.join(args.outDir, `${base}.margins.json`);
    fs.writeFileSync(pngPath, PNG.sync.write(padded));
    fs.writeFileSync(jsonPath, JSON.stringify(m, null, 2) + '\n');
    console.log(`[sprite-pad] ${src.width}x${src.height} -> ${m.paddedW}x${m.paddedH} (${pngPath})`);
    console.log(`[sprite-pad] margenes ${jsonPath}`);
}
if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
    main();
}
