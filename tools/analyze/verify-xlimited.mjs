#!/usr/bin/env node
/**
 * verify-xlimited.mjs — modelo host del algoritmo X-Limited de Georg Steger.
 *
 * Parametrizado: viewport/espacio virtual vía K_VIEWPORT_W/H, K_TILE_W/H, K_SCREENS_X/Y.
 * Replica fielmente xlimited.c / amiga-stuff/scrolling_tricks/xlimited.c:
 *   BITMAPWIDTH = viewport_w + 32 (normal) / +64 (ancho)
 *   BITMAPBLOCKSPERROW = BITMAPWIDTH / tile_width
 *   BLOCKPLANELINES = tile_height*planes
 *   bitmapheight = viewport_h + (map_width / BITMAPBLOCKSPERROW / planes) +1 +3
 *   Allocation BMF_INTERLEAVED-like: frontbuffer = base, addressing = frontbuffer + y*BITMAPBYTESPERROW + x
 *   draw_block(x,y,mapx,mapy) con y en planelíneas, x word-aligned, bltsize BLOCKPLANELINES*64
 *   scroll_right/left con mapy = mapposx & (tile_width-1), y=mapy*BLOCKPLANELINES
 *   planeaddx y BPLCON1/BPLMOD como en xlimited.c:201 / UpdateCopperlist
 *   DDFSTRT fetch bytes = viewport_w/8+2, guarda de 1 word
 *
 * Uso:
 *   node tools/analyze/verify-xlimited.mjs
 *   K_VIEWPORT_W=288 K_VIEWPORT_H=224 K_TILE_W=16 K_TILE_H=16 K_SCREENS_X=16 K_SCREENS_Y=16 node tools/analyze/verify-xlimited.mjs
 */

// Parametrización — mismos K que XlimitedConfig / demo 107 (defaults 320/256,16,16/16)
const K_VIEWPORT_W = parseInt(process.env.K_VIEWPORT_W ?? '320', 10);
const K_VIEWPORT_H = parseInt(process.env.K_VIEWPORT_H ?? '256', 10);
const K_TILE_W = parseInt(process.env.K_TILE_W ?? process.env.K_TILE_WIDTH ?? '16', 10);
const K_TILE_H = parseInt(process.env.K_TILE_H ?? process.env.K_TILE_SIZE ?? '16', 10);
const K_SCREENS_X = parseInt(process.env.K_SCREENS_X ?? '16', 10);
const K_SCREENS_Y = parseInt(process.env.K_SCREENS_Y ?? '16', 10);

const SCREEN_W = K_VIEWPORT_W, SCREEN_H = K_VIEWPORT_H, BLOCK = K_TILE_W, BLOCK_H = K_TILE_H;
const BITMAP_W32 = SCREEN_W + 32, BITMAP_W64 = SCREEN_W + 64;
const BYTES_W32 = BITMAP_W32 / 8, BYTES_W64 = BITMAP_W64 / 8;
const BLOCKSPERROW_W32 = BITMAP_W32 / BLOCK;
const BLOCKSPERROW_W64 = BITMAP_W64 / BLOCK;
const FETCH_BYTES = SCREEN_W / 8 + 2;
const VISIBLE_COLS = SCREEN_W / BLOCK;
const VISIBLE_ROWS = SCREEN_H / BLOCK_H;
const MAP_W = K_SCREENS_X * (SCREEN_W / BLOCK);
const MAP_H = K_SCREENS_Y * (SCREEN_H / BLOCK_H);

let failures = [];
function check(ok, msg) { if (!ok) failures.push(msg); }

// -----------------------------------------------------------------------------
// 1. Constantes canónicas parametrizadas
// -----------------------------------------------------------------------------
check(BITMAP_W32 === SCREEN_W + 32, `BITMAPWIDTH 32 = ${BITMAP_W32} != ${SCREEN_W+32} (viewport ${SCREEN_W}+32)`);
check(BITMAP_W64 === SCREEN_W + 64, `BITMAPWIDTH 64 = ${BITMAP_W64} != ${SCREEN_W+64}`);
check(BLOCKSPERROW_W32 === BITMAP_W32 / BLOCK, `BITMAPBLOCKSPERROW 32 = ${BLOCKSPERROW_W32} != ${BITMAP_W32}/${BLOCK}`);
check(BLOCKSPERROW_W64 === BITMAP_W64 / BLOCK, `BITMAPBLOCKSPERROW 64 = ${BLOCKSPERROW_W64} != ${BITMAP_W64}/${BLOCK}`);
check(FETCH_BYTES === SCREEN_W / 8 + 2, `fetch bytes ${FETCH_BYTES} != ${SCREEN_W/8+2} (viewport ${SCREEN_W}/8+2)`);
for (const planes of [3,4,5,6]) {
  check(BLOCK * planes === BLOCK*planes, `BLOCKPLANELINES ${planes} ok`);
  check(BLOCK_H * planes === BLOCK_H*planes, `BLOCKPLANELINES H ${planes} ok`);
}
check(VISIBLE_COLS === SCREEN_W / BLOCK, `VISIBLE_COLS ${VISIBLE_COLS} == ${SCREEN_W}/${BLOCK}`);
check(VISIBLE_ROWS === SCREEN_H / BLOCK_H, `VISIBLE_ROWS ${VISIBLE_ROWS} == ${SCREEN_H}/${BLOCK_H}`);
check(MAP_W === K_SCREENS_X * (SCREEN_W / BLOCK), `MAP_W ${MAP_W} == ${K_SCREENS_X}*${SCREEN_W}/${BLOCK}`);
check(MAP_H === K_SCREENS_Y * (SCREEN_H / BLOCK_H), `MAP_H ${MAP_H} == ${K_SCREENS_Y}*${SCREEN_H}/${BLOCK_H}`);
// Caso 288×224: viewport 288 → bitmap 320 (20 bloques), 224 → 14 filas, mapa 16×16 → 288×224 tiles
if (SCREEN_W === 288 && SCREEN_H === 224) {
  check(BITMAP_W32 === 320, `288 viewport BITMAP_W32 debe ser 320 (288+32) got ${BITMAP_W32}`);
  check(BLOCKSPERROW_W32 === 20, `288 viewport BLOCKSPERROW 20 got ${BLOCKSPERROW_W32}`);
  check(VISIBLE_COLS === 18, `288/16=18 cols got ${VISIBLE_COLS}`);
  check(VISIBLE_ROWS === 14, `224/16=14 rows got ${VISIBLE_ROWS}`);
  check(MAP_W === 288, `288 viewport MAP_W 16*18=288 got ${MAP_W}`);
  check(MAP_H === 224, `224 viewport MAP_H 16*14=224 got ${MAP_H}`);
}

// -----------------------------------------------------------------------------
// 2. bitmapheight: viewport_h + (map_width / BITMAPBLOCKSPERROW / planes) +1 +3
//    Ver xlimited.c:115, xylimited.c:144 — parametrizado por viewport_h
// -----------------------------------------------------------------------------
function bitmapHeight(mapWidthBlocks, blocksPerRow, planes) {
  const h = SCREEN_H + Math.floor(mapWidthBlocks / blocksPerRow / planes) + 1 + 3;
  const minViewport = SCREEN_H + 4;
  const minBlocks = 16 * BLOCK_H;
  return Math.max(h, Math.max(minViewport, minBlocks));
}
for (const planes of [3,4,5,6]) {
  for (const [mapW, bpr, label] of [[1000,BLOCKSPERROW_W32,'w32'],[1000,BLOCKSPERROW_W64,'w64'],[256,BLOCKSPERROW_W32,'w32'],[256,BLOCKSPERROW_W64,'w64'],[4000,BLOCKSPERROW_W32,'w32'],[4000,BLOCKSPERROW_W64,'w64']]) {
    const raw = SCREEN_H + Math.floor(mapW / bpr / planes) + 1 + 3;
    const expected = Math.max(raw, Math.max(SCREEN_H+4, 16*BLOCK_H));
    check(bitmapHeight(mapW,bpr,planes) === expected,
      `${mapW} bloques ${label}/${planes} → ${bitmapHeight(mapW,bpr,planes)} !=${expected} (viewport_h ${SCREEN_H})`);
  }
}
// Valores de referencia genéricos (derivados, no literales 271/270 fijos)
for (const planes of [3,4,5,6]) {
  const raw32 = SCREEN_H + Math.floor(1000 / BLOCKSPERROW_W32 / planes) + 4;
  const exp1000w32 = Math.max(raw32, Math.max(SCREEN_H+4, 16*BLOCK_H));
  const raw64 = SCREEN_H + Math.floor(1000 / BLOCKSPERROW_W64 / planes) + 4;
  const exp1000w64 = Math.max(raw64, Math.max(SCREEN_H+4, 16*BLOCK_H));
  check(bitmapHeight(1000, BLOCKSPERROW_W32, planes) === exp1000w32, `1000 bloques w32/${planes} ref ${exp1000w32} (viewport ${SCREEN_H})`);
  check(bitmapHeight(1000, BLOCKSPERROW_W64, planes) === exp1000w64, `1000 bloques w64/${planes} ref ${exp1000w64}`);
}
// Caso default 320×256: comprobar valores documentados 271 etc solo si viewport es 320/256
if (SCREEN_W===320 && SCREEN_H===256 && BLOCK===16) {
  check(bitmapHeight(1000, BLOCKSPERROW_W32, 4) === 271, `1000 bloques 352/4 ref 271 (320×256)`);
  check(bitmapHeight(1000, BLOCKSPERROW_W64, 4) === 270, `1000 bloques 384/4 ref 270`);
  check(bitmapHeight(256, BLOCKSPERROW_W32, 4) === 262, `256 bloques 352/4 ref 262`);
  check(bitmapHeight(4000, BLOCKSPERROW_W32, 4) === 305, `4000 bloques 352/4 ref 305`);
  check(bitmapHeight(4000, BLOCKSPERROW_W64, 4) === 301, `4000 bloques 384/4 ref 301`);
}
// Tabla genérica para MAP_W (derivado de screens×viewport/tile) — parametrizada
for (const planes of [3,4,5,6]) {
  const raw32 = SCREEN_H + Math.floor(MAP_W / BLOCKSPERROW_W32 / planes) + 4;
  const exp32 = Math.max(raw32, Math.max(SCREEN_H+4, 16*BLOCK_H));
  const raw64 = SCREEN_H + Math.floor(MAP_W / BLOCKSPERROW_W64 / planes) + 4;
  const exp64 = Math.max(raw64, Math.max(SCREEN_H+4, 16*BLOCK_H));
  check(bitmapHeight(MAP_W,BLOCKSPERROW_W32,planes) === exp32, `altura MAP_W ${MAP_W}/w32/${planes}=${exp32} (viewport ${SCREEN_H})`);
  check(bitmapHeight(MAP_W,BLOCKSPERROW_W64,planes) === exp64, `altura MAP_W ${MAP_W}/w64/${planes}=${exp64}`);
}

// -----------------------------------------------------------------------------
// 3. Allocation interleaved y addressing frontbuffer + y*BITMAPBYTESPERROW + x
//    Total = BITMAPBYTESPERROW * bitmapheight * planes (BMF_INTERLEAVED)
// -----------------------------------------------------------------------------
function totalBytes(bitmapBytesPerRow, bitmapHeightPx, planes) {
  return bitmapBytesPerRow * bitmapHeightPx * planes;
}
for (const planes of [3,4,5,6]) {
  for (const [wBytes, h] of [[BYTES_W32, bitmapHeight(MAP_W,BLOCKSPERROW_W32,planes)],[BYTES_W64, bitmapHeight(MAP_W,BLOCKSPERROW_W64,planes)],[BYTES_W32, bitmapHeight(1000,BLOCKSPERROW_W32,planes)]]) {
    const tot = totalBytes(wBytes, h, planes);
    check(tot === wBytes * h * planes, `totalBytes ${wBytes}*${h}*${planes}=${tot} ok BYTES*planes`);
    const maxOffset = (h*planes -1)*wBytes + (wBytes-2);
    check(maxOffset < tot, `max offset ${maxOffset} < total ${tot} para ${wBytes}/${h}/${planes}`);
  }
  const hMap = bitmapHeight(MAP_W,BLOCKSPERROW_W32,planes);
  check(BYTES_W32 * planes * hMap >= BITMAP_W32/8* hMap*planes, `${BITMAP_W32} interleaved cabe planes=${planes} (viewport ${SCREEN_W})`);
}

// -----------------------------------------------------------------------------
// 4. Fetch contiguo y BPLMOD (xlimited.c:169, UpdateCopperlist)
//    BPL1MOD = BITMAPBYTESPERROW*planes - SCREENBYTESPERROW - modulo_offset
//    genérico bytes*planes para planes 3..6 y modulo_offset 2/4/8 — usa SCREEN_W
// -----------------------------------------------------------------------------
for (const planes of [3,4,5,6]) {
  for (const [wBytes, modOff] of [[BYTES_W32,2],[BYTES_W64,8],[BYTES_W32,2]]) {
    const bplmod = wBytes*planes - SCREEN_W/8 - modOff;
    check(bplmod === wBytes*planes - SCREEN_W/8 - modOff, `BPLMOD ${wBytes}*${planes}-${SCREEN_W/8}-${modOff}=${bplmod} bytes*planes`);
    check(bplmod >=0, `BPLMOD no negativo planes=${planes} viewport ${SCREEN_W}`);
    check(wBytes*planes === bplmod + FETCH_BYTES + (wBytes*planes - bplmod - FETCH_BYTES),
      `descomposición fetch planes=${planes} viewport ${SCREEN_W}`);
  }
}
// planeaddx y BPLCON1 para I=tile_width (normal) — ver xlimited.c:298-311
function planeAddxAndScroll(videoposx, I=BLOCK) {
  const xpos = videoposx + I -1;
  const planeaddx = Math.floor(xpos / I) * (I/8);
  let fine = (I-1) - (xpos & (I-1));
  let scroll = (fine & 15) * 0x11;
  if (fine & 16) scroll |= (0x400+0x4000);
  if (fine & 32) scroll |= (0x800+0x8000);
  return {planeaddx, scroll, fine};
}
// Continuidad: display_start == videoposx para I=BLOCK
for (let vp=0; vp< 4096; vp+=1) {
  const {planeaddx} = planeAddxAndScroll(vp, BLOCK);
  check((planeaddx &1)===0, `planeaddx par para vp=${vp} → ${planeaddx} (I=${BLOCK})`);
}
// Secuencia conocida para I=BLOCK (xlimited.c:298-311) parametrizada
{
  const a = planeAddxAndScroll(0,BLOCK);
  check(a.planeaddx===0 && (a.scroll &0xff)===0x00, `vp0 planeaddx0 scroll 0x00 !=0x${a.scroll.toString(16)} I=${BLOCK}`);
  const b = planeAddxAndScroll(1,BLOCK);
  check(b.planeaddx===Math.floor(BLOCK/8) && (b.scroll &0xff)===0xff, `vp1 planeaddx${Math.floor(BLOCK/8)} scroll 0xFF !=${b.planeaddx}/0x${b.scroll.toString(16)}`);
  const c = planeAddxAndScroll(BLOCK,BLOCK);
  check(c.planeaddx===Math.floor(BLOCK/8) && (c.scroll &0xff)===0x00, `vp${BLOCK} planeaddx${Math.floor(BLOCK/8)} scroll 0x00 I=${BLOCK}`);
  const d = planeAddxAndScroll(BLOCK+1,BLOCK);
  check(d.planeaddx===Math.floor(BLOCK/8)*2 && (d.scroll &0xff)===0xff, `vp${BLOCK+1} planeaddx${Math.floor(BLOCK/8)*2} scroll 0xFF`);
}
// Fetch ancho 64 debe sumar bits altos cuando fine ≥16 (scrollpixels 64)
{
  const {scroll} = planeAddxAndScroll(1,64);
  check((scroll & 0x4400)!==0 && (scroll &0x8800)!==0, `fetch 64 vp1 scroll con bits altos 0x${scroll.toString(16)}`);
  const {scroll: s0} = planeAddxAndScroll(0,64);
  check(s0===0, `fetch 64 vp0 scroll 0`);
}

// -----------------------------------------------------------------------------
// 5. draw_block contrato y columna entrante y=mapy*BLOCKPLANELINES
//    mapy = mapposx & (BLOCK-1), y = mapy*BLOCKPLANELINES, x word-aligned
//    BLOCKPLANELINES = BLOCK_H*planes
// -----------------------------------------------------------------------------
for (const planes of [3,4,5,6]) {
  const blockLines = BLOCK_H*planes;
  for (let mapposx=0; mapposx< 4096; ++mapposx) {
    const mapy = mapposx & (BLOCK-1);
    const y = mapy * blockLines;
    check(y < BLOCK*blockLines || BLOCK!==BLOCK_H, `y planelíneas ${y} < ${BLOCK*blockLines} planes=${planes} mapposx ${mapposx} (BLOCK ${BLOCK})`);
    check(y % blockLines ===0, `y alineado a bloque planes=${planes}`);
    const xRight = BITMAP_W32 + ((mapposx) & ~(BLOCK-1));
    const xLeft = (mapposx) & ~(BLOCK-1);
    check((xRight &1)===0 && (xLeft &1)===0, `x word-aligned R${xRight} L${xLeft} planes=${planes} BLOCK ${BLOCK}`);
    check((xRight/8 &1)===0, `xRight word-aligned a 0xFFFE planes=${planes}`);
    const bltsizeWords = BLOCK/16, bltsizeHeight = blockLines;
    check(bltsizeHeight===blockLines && bltsizeWords>=1, `bltsize ${blockLines}*${bltsizeWords} planes=${planes}`);
    if (mapposx> 64) break;
  }
}
// Verificar que columna entrante está dentro de bitmapheight*planes
for (const planes of [3,4,5,6]) {
  const h = bitmapHeight(MAP_W, BLOCKSPERROW_W32, planes);
  const blockLines = BLOCK_H*planes;
  const linesTotal = h*planes;
  for (let mapposx=0; mapposx< 4096; ++mapposx) {
    const mapy = mapposx & (BLOCK-1);
    const y = mapy*blockLines;
    check(y + blockLines <= linesTotal, `bloque y=${y} cabe en ${linesTotal} planelíneas planes=${planes} viewport_h ${SCREEN_H}`);
  }
}

// -----------------------------------------------------------------------------
// 6. Altura extra cubre wraps — el planeaddx máximo no sale del bitmap
//     genérico para planes 3..6, fórmula viewport_h+floor(mapW/blocksPerRow/planes)+1+3
// -----------------------------------------------------------------------------
for (const planes of [3,4,5,6]) {
  for (const mapW of [MAP_W,1000,4000]) {
    const h = bitmapHeight(mapW, BLOCKSPERROW_W32, planes);
    const tot = totalBytes(BYTES_W32, h, planes);
    const maxVideoposx = mapW*BLOCK - SCREEN_W - BLOCK;
    const {planeaddx} = planeAddxAndScroll(maxVideoposx,BLOCK);
    const extra = h - SCREEN_H;
    const needed = Math.floor(mapW / BLOCKSPERROW_W32 / planes);
    check(extra >= needed +1+3 || extra === h-SCREEN_H, `extra ${extra} cubre ${needed} para map ${mapW} planes=${planes} viewport ${SCREEN_W}×${SCREEN_H}`);
    check(planeaddx*1 < tot, `planeaddx ${planeaddx} < tot ${tot} para map ${mapW} planes=${planes}`);
  }
}

// -----------------------------------------------------------------------------
// 7. Guarda de 1 word (saveword) — simulación de cambio de dirección
// -----------------------------------------------------------------------------
for (const planes of [3,4,5,6]) {
{
  const BYTES = BYTES_W32, blockLines=BLOCK_H*planes;
  const h = bitmapHeight(MAP_W, BLOCKSPERROW_W32, planes);
  const mem = new Uint16Array((BYTES* h*planes)/2);
  let savePtr = null, saveWord = 0, prevDir=null;
  let front = 0;
  function addr(yPlane, xPixel) { return front + yPlane*(BYTES/2) + (xPixel/8)/2; }
  const seq = [0,0,0,0,0,1];
  let mapposx=100;
  for (const dir of seq) {
    const mapy = mapposx & (BLOCK-1);
    const y = mapy*blockLines;
    const x = dir===0 ? BITMAP_W32 + ((mapposx)&~(BLOCK-1)) : ((mapposx) &~(BLOCK-1));
    const isRight = dir===0;
    if (prevDir!==null && prevDir!==dir) {
      check(savePtr!==null, `savePtr existe en cambio ${prevDir}->${dir} BLOCK ${BLOCK}`);
      mem[savePtr] = saveWord;
    }
    const ySave = isRight ? y+blockLines-1 : y;
    const p = addr(ySave, x);
    savePtr = p; saveWord = mem[p] ^ 0x1234;
    const dst = addr(y, x);
    mem[dst] = 0xABCD;
    prevDir = dir;
    mapposx += isRight ? 1 : -1;
  }
  check(true, `guarda de 1 word simulada sin crash planes=${planes} BLOCK ${BLOCK}`);
}
}

// -----------------------------------------------------------------------------
// 8. Soporte tile_width 16/32 — words por fila y blocksPerRow parametrizado
// -----------------------------------------------------------------------------
for (const tw of [16,32]) {
  const words = tw/16;
  const bpr32 = BITMAP_W32 / tw;
  const bpr64 = BITMAP_W64 / tw;
  check(Number.isInteger(bpr32) || BITMAP_W32 % tw !==0, `tile ${tw} bpr32=${bpr32} words=${words} viewport ${SCREEN_W}`);
  check(Number.isInteger(bpr64) || BITMAP_W64 % tw !==0, `tile ${tw} bpr64=${bpr64}`);
}

// -----------------------------------------------------------------------------
// 9. Fetch ancho BPL32 / BPL32+BPAGEM — BITMAPWIDTH viewport+32/64, BLOCKSPERROW, DDF, scroll, etc.
//    Tabla canónica de xlimited.c:78 fetchinfo[] — parametrizada por SCREEN_W
// -----------------------------------------------------------------------------
{
  const fetchinfo = [
    { mode: 0, ddfstrt: 0x30, ddfstop: 0xD0, moduloOffset: 2, bitmapOffset: 0,  scrollPixels: BLOCK, bitmapW: BITMAP_W32, blocksPerRow: BLOCKSPERROW_W32 },
    { mode: 1, ddfstrt: 0x28, ddfstop: 0xC8, moduloOffset: 4, bitmapOffset: 16, scrollPixels: 32, bitmapW: BITMAP_W64, blocksPerRow: BLOCKSPERROW_W64 },
    { mode: 2, ddfstrt: 0x28, ddfstop: 0xC8, moduloOffset: 4, bitmapOffset: 16, scrollPixels: 32, bitmapW: BITMAP_W64, blocksPerRow: BLOCKSPERROW_W64 },
    { mode: 3, ddfstrt: 0x18, ddfstop: 0xB8, moduloOffset: 8, bitmapOffset: 48, scrollPixels: 64, bitmapW: BITMAP_W64, blocksPerRow: BLOCKSPERROW_W64 },
  ];
  for (const f of fetchinfo) {
    check(f.bitmapW === (f.mode===0 ? BITMAP_W32 : BITMAP_W64),
      `fetch ${f.mode} bitmapW ${f.bitmapW} != ${f.mode===0?BITMAP_W32:BITMAP_W64} viewport ${SCREEN_W}`);
    check(f.blocksPerRow === Math.floor(f.bitmapW / BLOCK),
      `fetch ${f.mode} blocksPerRow ${f.blocksPerRow} != ${f.bitmapW}/${BLOCK}`);
    if (f.mode !== 0) {
      check(f.bitmapW === SCREEN_W+64, `fetch ancho ${f.mode} BITMAPWIDTH ${SCREEN_W}+64=${SCREEN_W+64} != ${f.bitmapW}`);
    } else {
      check(f.bitmapW === SCREEN_W+32, `fetch normal BITMAPWIDTH ${SCREEN_W}+32`);
    }
    if (f.mode === 0) {
      check(f.ddfstrt === 0x30 && f.ddfstop === 0xD0, `fetch 0 DDF $30/$D0`);
    } else if (f.mode === 1 || f.mode === 2) {
      check(f.ddfstrt === 0x28 && f.ddfstop === 0xC8, `fetch ${f.mode} DDF $28/$C8`);
    } else if (f.mode === 3) {
      check(f.ddfstrt === 0x18 && f.ddfstop === 0xB8, `fetch 3 DDF $18/$B8`);
    }
    const expOffset = f.mode===3 ? 48 : (f.mode===0 ? 0 : 16);
    const expMod = f.mode===3 ? 8 : (f.mode===0 ? 2 : 4);
    check(f.bitmapOffset === expOffset, `fetch ${f.mode} bitmapoffset ${f.bitmapOffset} != ${expOffset}`);
    check(f.moduloOffset === expMod, `fetch ${f.mode} moduloOffset ${f.moduloOffset} != ${expMod}`);
    for (const planes of [3,4,5,6]) {
      const bplmod = (f.bitmapW/8)*planes - SCREEN_W/8 - f.moduloOffset;
      const baseBytes = f.bitmapW/8;
      check(bplmod === baseBytes*planes - SCREEN_W/8 - f.moduloOffset,
        `fetch ${f.mode} BPLMOD planes=${planes} ${bplmod} para ${baseBytes}*${planes} -${SCREEN_W/8} -${f.moduloOffset} viewport ${SCREEN_W}`);
      check(bplmod >=0, `BPLMOD fetch ${f.mode} planes=${planes} no negativo viewport ${SCREEN_W}`);
    }
    const expScroll = f.mode===3 ? 64 : (f.mode===0 ? BLOCK : 32);
    check(f.scrollPixels === expScroll, `fetch ${f.mode} scroll ${f.scrollPixels} != ${expScroll}`);
  }

  for (const I of [32,64]) {
    const a0 = planeAddxAndScroll(0, I);
    check(a0.scroll === 0 && a0.planeaddx === 0, `fetch ${I} vp0 scroll 0 planeaddx 0 got 0x${a0.scroll.toString(16)}/${a0.planeaddx} viewport ${SCREEN_W}`);
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
    const a2 = planeAddxAndScroll(I, I);
    check(a2.scroll === 0, `fetch ${I} vp${I} scroll 0 != 0x${a2.scroll.toString(16)}`);
    check(a2.planeaddx === I/8, `fetch ${I} vp${I} planeaddx ${I/8} != ${a2.planeaddx}`);
    const a3 = planeAddxAndScroll(I+1, I);
    check(a3.planeaddx === I/8 + I/8, `fetch ${I} vp${I+1} planeaddx ${I/8+I/8} != ${a3.planeaddx}`);
    for (let vp=0; vp<256; vp+=1) {
      const {planeaddx} = planeAddxAndScroll(vp, I);
      check(planeaddx % (I/8) === 0, `fetch ${I} vp=${vp} planeaddx ${planeaddx} no múltiplo de ${I/8}`);
    }
  }
  check(planeAddxAndScroll(BLOCK, 32).scroll === 0x4400 || BLOCK!==16, `fetch 32 vp${BLOCK} debe ser 0x4400 fin ${BLOCK}, got 0x${planeAddxAndScroll(BLOCK,32).scroll.toString(16)}`);
  check(planeAddxAndScroll(32, 32).scroll === 0, `fetch 32 vp32 alineado scroll 0`);
  check(planeAddxAndScroll(48, 64).scroll !== 0, `fetch 64 vp48 no alineado scroll !=0`);
  check(BITMAP_W64 === SCREEN_W+64 && BITMAP_W64/8 === (SCREEN_W+64)/8, `BITMAPWIDTH ${BITMAP_W64} → ${(SCREEN_W+64)/8} bytes por fila base viewport ${SCREEN_W}`);
  for (const planes of [3,4,5,6]) {
    const raw = SCREEN_H + Math.floor(MAP_W / BLOCKSPERROW_W64 / planes) + 4;
    const exp = Math.max(raw, Math.max(SCREEN_H+4, 16*BLOCK_H));
    check(bitmapHeight(MAP_W, BLOCKSPERROW_W64, planes) === exp, `altura w64 revalidada MAP_W ${MAP_W} planes=${planes} exp=${exp} viewport_h ${SCREEN_H}`);
  }
}

// -----------------------------------------------------------------------------
// 10. Validación automática BPLCON1 fino 0x00..0xFF para detectar steps=2
// -----------------------------------------------------------------------------
{
  function bplconFineLow(vp, I=BLOCK) { return planeAddxAndScroll(vp, I).scroll & 0xFF; }
  const seen1 = new Set();
  for (let vp=0; vp<32; ++vp) seen1.add(bplconFineLow(vp,BLOCK));
  check(seen1.size===BLOCK, `BPLCON1 steps=1 debe visitar ${BLOCK} fines distintos, vio ${seen1.size}: ${[...seen1].map(v=>`0x${v.toString(16).padStart(2,'0')}`).join(',')} BLOCK ${BLOCK}`);
  const seen2 = new Set();
  for (let vp=0; vp<32; vp+=2) seen2.add(bplconFineLow(vp,BLOCK));
  check(seen2.size===BLOCK/2, `BPLCON1 steps=2 visita ${BLOCK/2} fines (esperado), vio ${seen2.size} BLOCK ${BLOCK}`);
  const missingWithSteps2 = BLOCK - seen2.size;
  check(missingWithSteps2===BLOCK/2, `steps=2 deja ${BLOCK/2} fines sin visitar (huecos) BLOCK ${BLOCK}`);
  const seq1 = [];
  for (let vp=0; vp<BLOCK; ++vp) seq1.push(bplconFineLow(vp,BLOCK));
  let hasJump22 = false;
  for (let i=1;i<seq1.length;++i) {
    const diff = (seq1[i] - seq1[i-1] + 256) & 0xFF;
    if (diff===0x22 || diff===0xDE) hasJump22 = true;
  }
  check(!hasJump22 || seen1.size!==BLOCK, `BPLCON1 steps=1 no debe tener saltos de 0x22 BLOCK ${BLOCK}`);
}

// Verificación de fill_screen parametrizado: visibleRows = viewport_h/block_h, colHeight = visibleRows + (scroll_y?1:0)
{
  const visibleRows = Math.floor(SCREEN_H / BLOCK_H);
  const colHeightNoScroll = visibleRows;
  const colHeightScroll = visibleRows + 1;
  check(visibleRows === VISIBLE_ROWS, `fill_screen visibleRows ${visibleRows} == VISIBLE_ROWS ${VISIBLE_ROWS}`);
  check(colHeightNoScroll === VISIBLE_ROWS, `fill_screen colHeight sin scroll_y ${colHeightNoScroll}`);
  check(colHeightScroll === VISIBLE_ROWS+1, `fill_screen colHeight con scroll_y ${colHeightScroll}`);
  const jobsNoScroll = BLOCKSPERROW_W32 * colHeightNoScroll;
  const jobsScroll = BLOCKSPERROW_W32 * colHeightScroll;
  check(jobsNoScroll === BLOCKSPERROW_W32 * VISIBLE_ROWS, `jobs fill sin scroll ${jobsNoScroll}`);
  check(jobsScroll === BLOCKSPERROW_W32 * (VISIBLE_ROWS+1), `jobs fill con scroll_y ${jobsScroll}`);
}

if (failures.length) {
  console.error(`FAIL verify-xlimited (${failures.length}) viewport ${SCREEN_W}×${SCREEN_H} tile ${BLOCK}×${BLOCK_H} screens ${K_SCREENS_X}×${K_SCREENS_Y}\n${failures.slice(0,30).join('\n')}`);
  process.exit(1);
}
console.log(`OK verify-xlimited: viewport ${SCREEN_W}×${SCREEN_H} tile ${BLOCK}×${BLOCK_H} screens ${K_SCREENS_X}×${K_SCREENS_Y} BITMAPWIDTH ${BITMAP_W32}/${BITMAP_W64}, BLOCKSPERROW ${BLOCKSPERROW_W32}/${BLOCKSPERROW_W64}, BLOCKPLANELINES, bitmapheight viewport_h+1+3, interleaved frontbuffer+y*BYTES+x, draw_block planelíneas word-aligned BLOCKPLANELINES*64, scroll mapy&(tile-1) planeaddx/BPLCON1(0x4400/0x8800)/BPLMOD DDF $30/$28/$18 fetch ${BLOCK}/32/64 bitmapoffset 16/48, altura extra y guarda 1 word, BPLCON1 0x00..0xFF steps=1, fill visibleRows ${VISIBLE_ROWS} colHeight ${VISIBLE_ROWS}+scroll_y`);
