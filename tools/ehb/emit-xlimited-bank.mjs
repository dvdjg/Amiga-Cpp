#!/usr/bin/env node
// Convierte el tilebank INDEXADO del pipeline EHB (out/ehb/tilebank.raw.bin,
// 1 B/píxel por tile, índice EHB 0..63 BASES-PRIMERO, stride tile*tile bytes)
// al BANCO DE BLOQUES INTERLEAVED que consume el motor X-Limited
// (`draw_block_job`): un bitmap de 320 px de ancho (40 B/planelínea) donde cada
// tile se coloca en (tile % (320/tw), tile / (320/tw)) y cada píxel se convierte
// de índice EHB a sus 6 planos (bit p del índice = plano p; bit 5 = half).
//
// POR QUÉ EN EL HOST (no en el Amiga): la conversión necesita en el Amiga el
// tilebank INDEXADO crudo (287 KB) MÁS el banco interleaved resultante (222 KB)
// MÁS el display (70+ KB) = > 512 KB de Chip RAM de un A500 OCS sin fast RAM.
// Convirtiendo en el host solo se incbinan en .MEMF_CHIP los 222 KB del banco
// listo; el Amiga no carga el raw y el scroll no paga ninguna transformación.
//
// Uso: node tools/ehb/emit-xlimited-bank.mjs [--tw 16] [--th 16] [--planes 6]
//                                           [--in out/ehb/tilebank.raw.bin]
//                                           [--out-bin out/ehb/tilebank.xlimited.bin]
//                                           [--out-h out/ehb/tilebank.xlimited.h]
import fs from 'node:fs';
import path from 'node:path';

const argV = (n, d) => { const i = process.argv.indexOf(n); return i >= 0 ? process.argv[i + 1] : d; };
const intV = (n, d) => parseInt(argV(n, d), 10);

const tw = intV('--tw', 16);
const th = intV('--th', 16);
const planes = intV('--planes', 6);
if (tw % 16 !== 0 || th !== 16 || planes !== 6) {
  console.error(`[emit-xlimited] solo soportado 16x16 x6 (EHB); tw=${tw} th=${th} planes=${planes}`);
  process.exit(1);
}

const inBin = path.resolve(argV('--in', 'out/ehb/tilebank.raw.bin'));
const outBin = path.resolve(argV('--out-bin', 'out/ehb/tilebank.xlimited.bin'));
const outH = path.resolve(argV('--out-h', 'out/ehb/tilebank.xlimited.h'));

const raw = fs.readFileSync(inBin);
const stride = tw * th;                       // 256 B/tile (16x16, 1 B/píxel)
if (raw.length % stride !== 0) { console.error(`[emit-xlimited] raw ${raw.length} no múltiplo de stride ${stride}`); process.exit(1); }
const tileCount = raw.length / stride;

// Layout del banco interleaved (320 px de ancho), igual que
// `xlimited_build_blocks_bitmap_from_indexed` del engine.
const srcBytesPerRow = 320 / 8;               // 40
const blocksPerRow = 320 / tw;                // 20 (tw=16)
const blockRows = Math.ceil(tileCount / blocksPerRow);
const height = blockRows * (th * planes);     // planelíneas totales
const outBytes = srcBytesPerRow * height;

const out = Buffer.alloc(outBytes, 0);
const twBytes = tw / 8;                       // 2 B por planelínea de tile

for (let tile = 0; tile < tileCount; ++tile) {
  const bx = tile % blocksPerRow;
  const by = Math.floor(tile / blocksPerRow);
  const basePl = by * (th * planes) * srcBytesPerRow;
  let src = tile * stride;
  for (let row = 0; row < th; ++row) {
    for (let plane = 0; plane < planes; ++plane) {
      const bit = 1 << plane;
      let word = 0;
      for (let c = 0; c < tw; ++c) {
        const v = raw[src + row * tw + c];
        if ((v & bit) !== 0) word |= 0x8000 >> c;
      }
      const planeline = row * planes + plane;
      const off = (basePl + planeline * srcBytesPerRow) + bx * twBytes;
      out[off] = (word >> 8) & 0xff;
      out[off + 1] = word & 0xff;
    }
  }
}

// Cabecera C++ mínima para incbinar el banco en .MEMF_CHIP.
const hdr = [
  '// Banco de bloques X-Limited interleaved (320 px ancho, 40 B/planelínea).',
  `// Generado por tools/ehb/emit-xlimited-bank.mjs a partir del tilebank raw`,
  `// INDEXADO (${tileCount} tiles de ${tw}x${th}, ${planes} planos EHB).`,
  `// Layout: tile -> (tile % ${blocksPerRow}, tile / ${blocksPerRow}) bloques de`,
  `// ${twBytes*8} px; interleaved por planelínea (row*${planes}+plane).`,
  `extern "C" const unsigned char g_tilebank_xlimited[];`,
  `extern "C" const unsigned int g_tilebank_xlimited_size;`,
  '',
].join('\n');

fs.writeFileSync(outBin, out);
fs.writeFileSync(outH, hdr, 'utf8');
console.log(`[emit-xlimited] ${outBin} (${tileCount} tiles -> ${outBytes} B, ${height} planelíneas)`);
console.log(`[emit-xlimited] ${outH}`);
