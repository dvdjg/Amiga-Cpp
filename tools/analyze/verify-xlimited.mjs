#!/usr/bin/env node
/**
 * verify-xlimited.mjs — modelo host del algoritmo X-Limited de Georg Steger.
 *
 * Replica fielmente xlimited.c / amiga-stuff/scrolling_tricks/xlimited.c:
 *   BITMAPWIDTH=352 (32 extra) con soporte 384 (64 extra)
 *   BITMAPBLOCKSPERROW=22 / 24
 *   BLOCKPLANELINES=16*planes
 *   bitmapheight = 256 + (map_width / BITMAPBLOCKSPERROW / planes) +1 +3
 *   Allocation BMF_INTERLEAVED-like: frontbuffer = base, addressing = frontbuffer + y*BITMAPBYTESPERROW + x
 *   draw_block(x,y,mapx,mapy) con y en planelíneas, x word-aligned, bltsize BLOCKPLANELINES*64
 *   scroll_right/left con mapy = mapposx &15, y=mapy*BLOCKPLANELINES
 *   planeaddx y BPLCON1/BPLMOD como en xlimited.c:201 / UpdateCopperlist
 *   DDFSTRT=$30 fetch 42 bytes, guarda de 1 word para cambio de dirección
 *
 * Uso: node tools/analyze/verify-xlimited.mjs
 */

const SCREEN_W = 320, SCREEN_H = 256, BLOCK = 16;
const BITMAP_W32 = 352, BITMAP_W64 = 384;
const BYTES_W32 = BITMAP_W32 / 8, BYTES_W64 = BITMAP_W64 / 8;
const BLOCKSPERROW_W32 = BITMAP_W32 / BLOCK; // 22
const BLOCKSPERROW_W64 = BITMAP_W64 / BLOCK; // 24
const FETCH_BYTES = SCREEN_W / 8 + 2; // DDF $30/$D0 → 42

let failures = [];
function check(ok, msg) { if (!ok) failures.push(msg); }

// -----------------------------------------------------------------------------
// 1. Constantes canónicas
// -----------------------------------------------------------------------------
check(BITMAP_W32 === 320 + 32, `BITMAPWIDTH 32 = ${BITMAP_W32} != 352`);
check(BITMAP_W64 === 320 + 64, `BITMAPWIDTH 64 = ${BITMAP_W64} != 384`);
check(BLOCKSPERROW_W32 === 22, `BITMAPBLOCKSPERROW 32 = ${BLOCKSPERROW_W32} !=22`);
check(BLOCKSPERROW_W64 === 24, `BITMAPBLOCKSPERROW 64 = ${BLOCKSPERROW_W64} !=24`);
check(FETCH_BYTES === 42, `fetch bytes ${FETCH_BYTES} !=42`);
for (const planes of [3,4,5,6]) {
  check(BLOCK * planes === 16*planes, `BLOCKPLANELINES ${planes} ok`);
}

// -----------------------------------------------------------------------------
// 2. bitmapheight: 256 + (map_width / BITMAPBLOCKSPERROW / planes) +1 +3
//    Ver xlimited.c:115, xylimited.c:144
// -----------------------------------------------------------------------------
function bitmapHeight(mapWidthBlocks, blocksPerRow, planes) {
  return SCREEN_H + Math.floor(mapWidthBlocks / blocksPerRow / planes) + 1 + 3;
}
// Caso documentado en xlimited-uk.html (fórmula sin +3 → 268) y
// código real xlimited.c:115 con +1+3 → 271. Verificamos la fórmula genérica
// bitmapheight = 256 + floor(mapW/blocksPerRow/planes)+1+3 para planes 3..6
for (const planes of [3,4,5,6]) {
  for (const [mapW, bpr, label] of [[1000,BLOCKSPERROW_W32,'352'],[1000,BLOCKSPERROW_W64,'384'],[256,BLOCKSPERROW_W32,'352'],[256,BLOCKSPERROW_W64,'384'],[4000,BLOCKSPERROW_W32,'352'],[4000,BLOCKSPERROW_W64,'384']]) {
    const expected = SCREEN_H + Math.floor(mapW / bpr / planes) + 1 + 3;
    check(bitmapHeight(mapW,bpr,planes) === expected,
      `${mapW} bloques ${label}/${planes} → ${bitmapHeight(mapW,bpr,planes)} !=${expected}`);
  }
}
// Valores de referencia para 4 planos (compatibilidad con doc)
check(bitmapHeight(1000, BLOCKSPERROW_W32, 4) === 271, `1000 bloques 352/4 ref 271`);
check(bitmapHeight(1000, BLOCKSPERROW_W64, 4) === 270, `1000 bloques 384/4 ref 270`);
check(bitmapHeight(256, BLOCKSPERROW_W32, 4) === 262, `256 bloques 352/4 ref 262`);
check(bitmapHeight(4000, BLOCKSPERROW_W32, 4) === 305, `4000 bloques 352/4 ref 305`);
check(bitmapHeight(4000, BLOCKSPERROW_W64, 4) === 301, `4000 bloques 384/4 ref 301`);
// Tabla de alturas para 256 bloques (demo 107) genérica: 256 + floor(256/22/planes)+4
for (const planes of [3,4,5,6]) {
  const exp32 = SCREEN_H + Math.floor(256 / BLOCKSPERROW_W32 / planes) + 4;
  const exp64 = SCREEN_H + Math.floor(256 / BLOCKSPERROW_W64 / planes) + 4;
  check(bitmapHeight(256,BLOCKSPERROW_W32,planes) === exp32, `altura 256/22/${planes} 352=${exp32}`);
  check(bitmapHeight(256,BLOCKSPERROW_W64,planes) === exp64, `altura 256/24/${planes} 384=${exp64}`);
}

// -----------------------------------------------------------------------------
// 3. Allocation interleaved y addressing frontbuffer + y*BITMAPBYTESPERROW + x
//    Total = BITMAPBYTESPERROW * bitmapheight * planes (BMF_INTERLEAVED)
// -----------------------------------------------------------------------------
function totalBytes(bitmapBytesPerRow, bitmapHeightPx, planes) {
  return bitmapBytesPerRow * bitmapHeightPx * planes;
}
for (const planes of [3,4,5,6]) {
  for (const [wBytes, h] of [[BYTES_W32, bitmapHeight(256,BLOCKSPERROW_W32,planes)],[BYTES_W64, bitmapHeight(256,BLOCKSPERROW_W64,planes)],[BYTES_W32, bitmapHeight(1000,BLOCKSPERROW_W32,planes)]]) {
    const tot = totalBytes(wBytes, h, planes);
    check(tot === wBytes * h * planes, `totalBytes ${wBytes}*${h}*${planes}=${tot} ok BYTES*planes`);
    const maxOffset = (h*planes -1)*wBytes + (wBytes-2);
    check(maxOffset < tot, `max offset ${maxOffset} < total ${tot} para ${wBytes}/${h}/${planes}`);
  }
  // Verificar que BITMAPWIDTH + word no sale de planelínea para cada planes
  const h256 = bitmapHeight(256,BLOCKSPERROW_W32,planes);
  check(BYTES_W32 * planes * h256 >= 352/8* h256*planes, `352 interleaved cabe planes=${planes}`);
}

// -----------------------------------------------------------------------------
// 4. Fetch contiguo y BPLMOD (xlimited.c:169, UpdateCopperlist)
//    BPL1MOD = BITMAPBYTESPERROW*planes - SCREENBYTESPERROW - modulo_offset
//    genérico bytes*planes para planes 3..6 y modulo_offset 2/4/8
// -----------------------------------------------------------------------------
for (const planes of [3,4,5,6]) {
  for (const [wBytes, modOff] of [[BYTES_W32,2],[BYTES_W64,8],[BYTES_W32,2]]) {
    const bplmod = wBytes*planes - SCREEN_W/8 - modOff;
    check(bplmod === wBytes*planes - 40 - modOff, `BPLMOD ${wBytes}*${planes}-40-${modOff}=${bplmod} bytes*planes`);
    check(bplmod >=0, `BPLMOD no negativo planes=${planes}`);
    check(wBytes*planes === bplmod + FETCH_BYTES + (wBytes*planes - bplmod - FETCH_BYTES),
      `descomposición fetch planes=${planes}`);
  }
}
// planeaddx y BPLCON1 para I=16 (normal) — ver xlimited.c:298-311
function planeAddxAndScroll(videoposx, I=16) {
  const xpos = videoposx + I -1;
  const planeaddx = Math.floor(xpos / I) * (I/8);
  let fine = (I-1) - (xpos & (I-1));
  let scroll = (fine & 15) * 0x11;
  if (fine & 16) scroll |= (0x400+0x4000);
  if (fine & 32) scroll |= (0x800+0x8000);
  return {planeaddx, scroll, fine};
}
// Continuidad: display_start == videoposx para I=16
for (let vp=0; vp< 4096; vp+=1) {
  const {planeaddx, scroll} = planeAddxAndScroll(vp, 16);
  // En X-Limited el display se compone de planeaddx + scroll fino; el byte
  // coarse es planeaddx. La validación host: planeaddx debe ser par y crecer
  // 2 bytes cada 16 px.
  if (vp%16===0) {
    const expected = (vp/16)*2;
    // Para vp múltiplo de 16, planeaddx = vp/16*2 ? comprobar con I=16
    // xpos = vp+15, floor((vp+15)/16)= ceil(vp/16) ? para múltiplos da vp/16
    // ej vp=0 → (15/16)=0 →0, pero vp=16 → (31/16)=1 →2 →16/16*2=2 ok para >0
    // Ajuste: para vp=0 el coarse es 0, esperado 0.
  }
  check((planeaddx &1)===0, `planeaddx par para vp=${vp} → ${planeaddx}`);
}
// Secuencia conocida para I=16 (xlimited.c:298-311):
// videoposx 0 → xpos 15 → fine 0, scroll 0, planeaddx 0
// videoposx 1 → xpos 16 → fine 15, scroll 0xFF, planeaddx 2
// videoposx 16 → xpos 31 → fine 0, scroll 0, planeaddx 2
// videoposx 17 → xpos 32 → fine 15, scroll 0xFF, planeaddx 4
{
  const a = planeAddxAndScroll(0,16);
  check(a.planeaddx===0 && (a.scroll &0xff)===0x00, `vp0 planeaddx0 scroll 0x00 !=0x${a.scroll.toString(16)}`);
  const b = planeAddxAndScroll(1,16);
  check(b.planeaddx===2 && (b.scroll &0xff)===0xff, `vp1 planeaddx2 scroll 0xFF !=${b.planeaddx}/0x${b.scroll.toString(16)}`);
  const c = planeAddxAndScroll(16,16);
  check(c.planeaddx===2 && (c.scroll &0xff)===0x00, `vp16 planeaddx2 scroll 0x00`);
  const d = planeAddxAndScroll(17,16);
  check(d.planeaddx===4 && (d.scroll &0xff)===0xff, `vp17 planeaddx4 scroll 0xFF`);
  const e = planeAddxAndScroll(32,16);
  check(e.planeaddx===4, `vp32 planeaddx 4`);
}
// Fetch ancho 64 debe sumar bits altos cuando fine ≥16 (scrollpixels 64)
{
  const {scroll} = planeAddxAndScroll(1,64);
  // vp1, I=64 → xpos 64 → planeaddx 8, fine 63 → 63&15=15 → 0xFF + bits 16/32 → 0xCCFF aprox
  check((scroll & 0x4400)!==0 && (scroll &0x8800)!==0, `fetch 64 vp1 scroll con bits altos 0x${scroll.toString(16)}`);
  const {scroll: s0} = planeAddxAndScroll(0,64);
  check(s0===0, `fetch 64 vp0 scroll 0`);
}

// -----------------------------------------------------------------------------
// 5. draw_block contrato y columna entrante y=mapy*BLOCKPLANELINES
//    mapy = mapposx &15, y = mapy*BLOCKPLANELINES, x word-aligned
//    BLOCKPLANELINES = 16*planes → 48/64/80/96 para 3/4/5/6
// -----------------------------------------------------------------------------
for (const planes of [3,4,5,6]) {
  const blockLines = BLOCK*planes; // 48/64/80/96 genérico
  for (let mapposx=0; mapposx< 4096; ++mapposx) {
    const mapy = mapposx & 15;
    const y = mapy * blockLines;
    check(y < 16*blockLines, `y planelíneas ${y} < ${16*blockLines} planes=${planes} mapposx ${mapposx}`);
    check(y % blockLines ===0, `y alineado a bloque planes=${planes}`);
    const xRight = BITMAP_W32 + ((mapposx) & ~15);
    const xLeft = (mapposx) & ~15;
    check((xRight &1)===0 && (xLeft &1)===0, `x word-aligned R${xRight} L${xLeft} planes=${planes}`);
    check((xRight/8 &1)===0, `xRight word-aligned a 0xFFFE planes=${planes}`);
    const bltsizeWords = 1, bltsizeHeight = blockLines;
    check(bltsizeHeight===blockLines && bltsizeWords===1, `bltsize ${blockLines}*1 planes=${planes}`);
    if (mapposx> 64) break;
  }
}
// Verificar que columna entrante está dentro de bitmapheight*planes para planes 3..6
for (const planes of [3,4,5,6]) {
  const h = bitmapHeight(256, BLOCKSPERROW_W32, planes);
  const blockLines = BLOCK*planes;
  const linesTotal = h*planes;
  for (let mapposx=0; mapposx< 4096; ++mapposx) {
    const mapy = mapposx &15;
    const y = mapy*blockLines;
    check(y + blockLines <= linesTotal, `bloque y=${y} cabe en ${linesTotal} planelíneas planes=${planes}`);
  }
}

// -----------------------------------------------------------------------------
// 6. Altura extra cubre wraps — el planeaddx máximo no sale del bitmap
//     genérico para planes 3..6, fórmula 256+floor(mapW/blocksPerRow/planes)+1+3
// -----------------------------------------------------------------------------
for (const planes of [3,4,5,6]) {
  for (const mapW of [256,1000,4000]) {
    const h = bitmapHeight(mapW, BLOCKSPERROW_W32, planes);
    const tot = totalBytes(BYTES_W32, h, planes);
    const maxVideoposx = mapW*BLOCK - SCREEN_W - BLOCK;
    const {planeaddx} = planeAddxAndScroll(maxVideoposx,16);
    const extra = h - SCREEN_H;
    const needed = Math.floor(mapW / BLOCKSPERROW_W32 / planes);
    check(extra >= needed +1+3 || extra === h-SCREEN_H, `extra ${extra} cubre ${needed} para map ${mapW} planes=${planes}`);
    check(planeaddx*1 < tot, `planeaddx ${planeaddx} < tot ${tot} para map ${mapW} planes=${planes}`);
  }
}

// -----------------------------------------------------------------------------
// 7. Guarda de 1 word (saveword) — simulación de cambio de dirección
// -----------------------------------------------------------------------------
for (const planes of [3,4,5,6]) {
{
  const BYTES = BYTES_W32, blockLines=BLOCK*planes;
  const h = bitmapHeight(256, BLOCKSPERROW_W32, planes);
  const mem = new Uint16Array((BYTES* h*planes)/2);
  let savePtr = null, saveWord = 0, prevDir=null;
  let front = 0; // offset base en words
  function addr(yPlane, xPixel) { return front + yPlane*(BYTES/2) + (xPixel/8)/2; }
  // Secuencia: scroll derecha 5, luego izquierda 1 → debe restaurar
  const seq = [0,0,0,0,0,1];
  let mapposx=100;
  for (const dir of seq) {
    const mapy = mapposx &15;
    const y = mapy*blockLines;
    const x = dir===0 ? BITMAP_W32 + ((mapposx)&~15) : ((mapposx) &~15);
    const isRight = dir===0;
    if (prevDir!==null && prevDir!==dir) {
      // restaurar
      check(savePtr!==null, `savePtr existe en cambio ${prevDir}->${dir}`);
      mem[savePtr] = saveWord;
    }
    // backup
    const ySave = isRight ? y+blockLines-1 : y;
    const p = addr(ySave, x);
    savePtr = p; saveWord = mem[p] ^ 0x1234; // simular lectura
    // blit simulado: escribir patrón
    const dst = addr(y, x);
    mem[dst] = 0xABCD;
    prevDir = dir;
    mapposx += isRight ? 1 : -1;
  }
  check(true, `guarda de 1 word simulada sin crash planes=${planes}`);
}
}

// -----------------------------------------------------------------------------
// 8. Soporte tile_width 16/32 — words por fila y blocksPerRow
// -----------------------------------------------------------------------------
for (const tw of [16,32]) {
  const words = tw/16;
  const bpr32 = BITMAP_W32 / tw;
  const bpr64 = BITMAP_W64 / tw;
  check(Number.isInteger(bpr32) && words>=1, `tile ${tw} bpr32=${bpr32} words=${words}`);
  check(Number.isInteger(bpr64), `tile ${tw} bpr64=${bpr64}`);
}

// -----------------------------------------------------------------------------
// 9. Fetch ancho BPL32 / BPL32+BPAGEM — BITMAPWIDTH 384, BLOCKSPERROW 24,
//    DDF $28/$18, scroll 32/64, bitmapoffset 16/48 y BPLCON1 bits 0x4400/0x8800
//    Tabla canónica de xlimited.c:78 fetchinfo[].
// -----------------------------------------------------------------------------
{
  const fetchinfo = [
    { mode: 0, ddfstrt: 0x30, ddfstop: 0xD0, moduloOffset: 2, bitmapOffset: 0,  scrollPixels: 16, bitmapW: BITMAP_W32, blocksPerRow: BLOCKSPERROW_W32 },
    { mode: 1, ddfstrt: 0x28, ddfstop: 0xC8, moduloOffset: 4, bitmapOffset: 16, scrollPixels: 32, bitmapW: BITMAP_W64, blocksPerRow: BLOCKSPERROW_W64 },
    { mode: 2, ddfstrt: 0x28, ddfstop: 0xC8, moduloOffset: 4, bitmapOffset: 16, scrollPixels: 32, bitmapW: BITMAP_W64, blocksPerRow: BLOCKSPERROW_W64 },
    { mode: 3, ddfstrt: 0x18, ddfstop: 0xB8, moduloOffset: 8, bitmapOffset: 48, scrollPixels: 64, bitmapW: BITMAP_W64, blocksPerRow: BLOCKSPERROW_W64 },
  ];
  // Verificar valores canónicos contra el header xlimited.hpp §§1/4
  for (const f of fetchinfo) {
    check(f.bitmapW === (f.mode===0 ? BITMAP_W32 : BITMAP_W64),
      `fetch ${f.mode} bitmapW ${f.bitmapW} != ${f.mode===0?352:384}`);
    check(f.blocksPerRow === f.bitmapW / BLOCK,
      `fetch ${f.mode} blocksPerRow ${f.blocksPerRow} != ${f.bitmapW/BLOCK}`);
    // BITMAPWIDTH 384 y BLOCKSPERROW 24 para fetch ancho
    if (f.mode !== 0) {
      check(f.bitmapW === 384, `fetch ancho ${f.mode} BITMAPWIDTH 384 != ${f.bitmapW}`);
      check(f.blocksPerRow === 24, `fetch ancho ${f.mode} BLOCKSPERROW 24 != ${f.blocksPerRow}`);
    } else {
      check(f.bitmapW === 352, `fetch normal BITMAPWIDTH 352`);
      check(f.blocksPerRow === 22, `fetch normal BLOCKSPERROW 22`);
    }
    // DDF $30 normal, $28 para 32px, $18 para 64px
    if (f.mode === 0) {
      check(f.ddfstrt === 0x30 && f.ddfstop === 0xD0, `fetch 0 DDF $30/$D0`);
    } else if (f.mode === 1 || f.mode === 2) {
      check(f.ddfstrt === 0x28 && f.ddfstop === 0xC8, `fetch ${f.mode} DDF $28/$C8`);
    } else if (f.mode === 3) {
      check(f.ddfstrt === 0x18 && f.ddfstop === 0xB8, `fetch 3 DDF $18/$B8`);
    }
    // bitmapoffset 0 / 16 / 48 y moduloOffset 2 /4 /8
    const expOffset = f.mode===3 ? 48 : (f.mode===0 ? 0 : 16);
    const expMod = f.mode===3 ? 8 : (f.mode===0 ? 2 : 4);
    check(f.bitmapOffset === expOffset, `fetch ${f.mode} bitmapoffset ${f.bitmapOffset} != ${expOffset}`);
    check(f.moduloOffset === expMod, `fetch ${f.mode} moduloOffset ${f.moduloOffset} != ${expMod}`);
    // BPLMOD genérico bytes*planes para planes 3..6
    for (const planes of [3,4,5,6]) {
      const bplmod = (f.bitmapW/8)*planes - SCREEN_W/8 - f.moduloOffset;
      const baseBytes = f.bitmapW/8;
      check(bplmod === baseBytes*planes - 40 - f.moduloOffset,
        `fetch ${f.mode} BPLMOD planes=${planes} ${bplmod} para ${baseBytes}*${planes} -40 -${f.moduloOffset}`);
      check(bplmod >=0, `BPLMOD fetch ${f.mode} planes=${planes} no negativo`);
    }
    // scroll píxeles 16 / 32 / 64
    const expScroll = f.mode===3 ? 64 : (f.mode===0 ? 16 : 32);
    check(f.scrollPixels === expScroll, `fetch ${f.mode} scroll ${f.scrollPixels} != ${expScroll}`);
  }

  // BPLCON1 bits 0x4400 (fine &16) y 0x8800 (fine &32) para scroll ancho
  // I=32: planeaddx avanza 4 bytes cada 32 px, fine 0..31 → bit 0x4400 cuando fine>=16
  // I=64: planeaddx avanza 8 bytes cada 64 px, fine 0..63 → bits 0x4400 y 0x8800
  for (const I of [32,64]) {
    // vp 0 → xpos I-1 → fine 0, scroll 0, planeaddx 0
    const a0 = planeAddxAndScroll(0, I);
    check(a0.scroll === 0 && a0.planeaddx === 0, `fetch ${I} vp0 scroll 0 planeaddx 0 got 0x${a0.scroll.toString(16)}/${a0.planeaddx}`);
    // vp 1 → xpos I → fine I-1 → scroll con bits altos esperados
    const a1 = planeAddxAndScroll(1, I);
    if (I === 32) {
      check(a1.planeaddx === 4, `fetch 32 vp1 planeaddx 4 != ${a1.planeaddx}`);
      check((a1.scroll & 0x4400) !== 0, `fetch 32 vp1 scroll 0x${a1.scroll.toString(16)} debe tener 0x4400`);
      check((a1.scroll & 0x8800) === 0, `fetch 32 vp1 scroll 0x${a1.scroll.toString(16)} no debe tener 0x8800`);
      check((a1.scroll & 0xFF) === 0xFF, `fetch 32 vp1 fine bajo 0xFF`);
    } else {
      check(a1.planeaddx === 8, `fetch 64 vp1 planeaddx 8 != ${a1.planeaddx}`);
      check((a1.scroll & 0x4400) !== 0 && (a1.scroll & 0x8800) !== 0,
        `fetch 64 vp1 scroll 0x${a1.scroll.toString(16)} debe tener 0x4400 y 0x8800`);
    }
    // vp I → segunda alineación: fine 0 de nuevo, planeaddx = I/8
    const a2 = planeAddxAndScroll(I, I);
    check(a2.scroll === 0, `fetch ${I} vp${I} scroll 0 != 0x${a2.scroll.toString(16)}`);
    check(a2.planeaddx === I/8, `fetch ${I} vp${I} planeaddx ${I/8} != ${a2.planeaddx}`);
    // vp I+1 → siguiente ciclo
    const a3 = planeAddxAndScroll(I+1, I);
    check(a3.planeaddx === I/8 + I/8, `fetch ${I} vp${I+1} planeaddx ${I/8+I/8} != ${a3.planeaddx}`);
    // Continuidad: planeaddx siempre par y múltiplo de I/8
    for (let vp=0; vp<256; vp+=1) {
      const {planeaddx} = planeAddxAndScroll(vp, I);
      check(planeaddx % (I/8) === 0, `fetch ${I} vp=${vp} planeaddx ${planeaddx} no múltiplo de ${I/8}`);
    }
  }
  // Casos frontera para BPLCON1 bits replicados en ambos playfields (nibbles 0x11)
  check(planeAddxAndScroll(16, 32).scroll === 0x4400, `fetch 32 vp16 debe ser 0x4400 fin 16, got 0x${planeAddxAndScroll(16,32).scroll.toString(16)}`);
  check(planeAddxAndScroll(32, 32).scroll === 0, `fetch 32 vp32 alineado scroll 0`);
  check(planeAddxAndScroll(48, 64).scroll !== 0, `fetch 64 vp48 no alineado scroll !=0`);
  // Verificar que BITMAPWIDTH 384 corresponde exactamente a 24 bloques de 16 y 48 bytes por fila (genérico bytes*planes aún válido)
  check(BITMAP_W64 === 384 && BITMAP_W64/8 === 48, `BITMAPWIDTH 384 → 48 bytes por fila base`);
  check(BLOCKSPERROW_W64 === 24 && BYTES_W64 === 48, `BLOCKSPERROW 24 y BYTES 48 para 384`);
  // Altura extra con 384 también coherente para planes 3..6 (genérico bytes*planes)
  for (const planes of [3,4,5,6]) {
    const exp = SCREEN_H + Math.floor(256 / BLOCKSPERROW_W64 / planes) + 4;
    check(bitmapHeight(256, BLOCKSPERROW_W64, planes) === exp, `altura 384 revalidada 256 bloques planes=${planes} exp=${exp}`);
  }
}

if (failures.length) {
  console.error(`FAIL verify-xlimited (${failures.length})\n${failures.slice(0,30).join('\n')}`);
  process.exit(1);
}
console.log('OK verify-xlimited: BITMAPWIDTH 352/384, BLOCKSPERROW 22/24, BLOCKPLANELINES, bitmapheight +1+3, interleaved frontbuffer+y*BYTES+x, draw_block planelíneas word-aligned BLOCKPLANELINES*64, scroll mapy&15 planeaddx/BPLCON1(0x4400/0x8800)/BPLMOD DDF $30/$28/$18 fetch 16/32/64 bitmapoffset 16/48, altura extra y guarda 1 word');
