// ============================================================================
// test-sprite-pad: márgenes del FastCopy y hoja con padding (F5)
// ============================================================================
//
// Valida `tools/assets/sprite-pad.ts` (cocina host de F5) y su paridad con el
// helper del engine `fast_bob_margins` (HOST-437):
//   - fórmula de márgenes (pasos por eje, doble buffer) con los MISMOS valores
//     que el test C++ (24x12 con 3/5/2/1 doble -> 6/10/4/2, 40x18);
//   - hoja con padding transparente: tamaño final, borde alpha=0 y contenido
//     desplazado a (left, top).
//
//   node dist/tools/assets/test-sprite-pad.js
import { PNG } from 'pngjs';
import { fastBobMargins, padSprite } from './sprite-pad.js';
let failures = 0;
function check(ok, msg) {
    if (!ok) {
        console.error(`  [FAIL] ${msg}`);
        failures++;
    }
}
function solidPng(w, h, rgba) {
    const png = new PNG({ width: w, height: h });
    for (let i = 0; i < w * h; i++) {
        png.data[i * 4] = rgba[0];
        png.data[i * 4 + 1] = rgba[1];
        png.data[i * 4 + 2] = rgba[2];
        png.data[i * 4 + 3] = rgba[3];
    }
    return png;
}
function testMargins() {
    console.log('sprite-pad: formula de margenes (paridad HOST-437)');
    const a = fastBobMargins(16, 8, 1, 1, 1, 1, false);
    check(a.left === 1 && a.right === 1 && a.top === 1 && a.bottom === 1, '1 px por lado');
    check(a.paddedW === 18 && a.paddedH === 10, 'tamano con padding');
    const b = fastBobMargins(24, 12, 3, 5, 2, 1, true);
    check(b.left === 6 && b.right === 10 && b.top === 4 && b.bottom === 2, 'doble buffer dobla');
    check(b.paddedW === 40 && b.paddedH === 18, 'tamano doble buffer');
    const c = fastBobMargins(16, 16, 0, 0, 0, 0, false);
    check(c.paddedW === 16 && c.paddedH === 16, 'sin movimiento: sin margenes');
}
function testPad() {
    console.log('sprite-pad: hoja con padding transparente');
    const src = solidPng(2, 2, [255, 0, 0, 255]);
    const m = fastBobMargins(src.width, src.height, 3, 1, 2, 0, false);
    const out = padSprite(src, m);
    check(out.width === 6 && out.height === 4, 'tamano final 6x4');
    // Esquinas transparentes.
    const corner = (x, y) => out.data[(out.width * y + x) * 4 + 3];
    check(corner(0, 0) === 0 && corner(5, 3) === 0, 'borde transparente');
    // Contenido desplazado a (left, top) = (3, 2).
    const di = (out.width * 2 + 3) * 4;
    check(out.data[di] === 255 && out.data[di + 1] === 0 && out.data[di + 3] === 255, 'contenido en (left, top)');
    // Doble buffer: el doble de padding (2+6+2 = 10 x 2+4+0 = 6).
    const m2 = fastBobMargins(src.width, src.height, 3, 1, 2, 0, true);
    const out2 = padSprite(src, m2);
    check(out2.width === 10 && out2.height === 6, 'doble buffer: 10x6');
}
function main() {
    testMargins();
    testPad();
    if (failures === 0) {
        console.log('OK: sprite-pad (margenes + padding) validado.');
        process.exit(0);
    }
    console.error(`FAIL: ${failures} comprobacion(es) fallaron`);
    process.exit(1);
}
main();
