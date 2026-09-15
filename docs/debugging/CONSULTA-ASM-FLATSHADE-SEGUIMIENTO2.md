# Seguimiento 2  — visibilidad del ASM: caras/luz y draw

Apliqué tu plan. Hay datos nuevos y decisivos.

## 1. El frame negro tiene 0 aristas (tu hipótesis 1, confirmada)

Con la ruta ASM, leyendo `g_eng_prof` en el frame negro:

```
clear=40066 transform=120800 edges=0 fill=1252 update=172532 nEdges=0 nLines=0
```

`nEdges=0`, `nLines=0` → **`draw_edges` no tiene ninguna arista con `edgeColor > 0`**
→ no dibuja → pantalla negra. No es el transform matemático (sus nodos coinciden).

## 2. Comparación de flags a la MISMA fase (`m_angle=1000`)

Volqué `face->flags` (por `faceGroups`) y `edge->flags` (por `edgeGroups`) en C++ y ASM:

**C++** — caras visibles (`flags>=0`) = **9**, aristas con flag≠0 = **21**:
```
f1380=2  f1516=3  f1584=8  f1686=15  f1856=9  f1992=3  f2090=11  f2300=6  f2330=6   (resto -1)
e840=3 e852=1 e864=3 e894=2 e900=8 e912=2 e924=4 e930=6 e942=7 e954=2 e1104=3
e1134=9 e1230=15 e1242=6 e1254=5 e1260=3 e1272=9 e1284=5 e1290=6 e1302=6 e1368=3
```

**ASM** — caras visibles = **10**, aristas con flag≠0 = **17**:
```
f1380=2  f1516=4  f1584=10  f1686=14  f1856=9  f1992=3  f2090=10  f2210=2  f2300=2  f2330=6   (resto -1)
e840=4 e852=6 e864=4 e900=14 e912=3 e924=4 e948=8 e1104=6 e1116=2 e1128=11
e1236=12 e1248=2 e1260=3 e1272=8 e1284=5 e1296=3 e1368=3
```

Diferencias notables:
- **La luz está off-by-1/2** en varias caras: `f1516 3→4`, `f1584 8→10`, `f1686 15→14`,
  `f2090 11→10`, `f2300 6→2`; otras coinciden (`f1380=2`, `f1856=9`, `f1992=3`, `f2330=6`).
- **Un culling distinto**: `f2210` es **-1 en C++** y **2 (visible) en ASM** → el signo de `v`
  difiere en el límite (v≈0).
- Consecuencia: el **XOR de aristas** da distinto (21 vs 17), y en ciertos ángulos puede
  cancelar a 0 → negro.

## 3. El transform NO es el problema (los `vertex.*` coinciden)

Confirmado antes: la salida de `fs_transform_vertices` (ASM) es idéntica a C++ a fase fija.

## Preguntas

1. **¿De dónde sale el off-by-1/2 de la luz?** El cálculo es
   `s = hi16(px²+py²+pz²)` (clamp 511) y
   `res = mulu16((u16)(s16)hi16(v), kInvSqrt[s]) >> 16`, con
   `v = mul16(n0,px)+mul16(n1,py)+mul16(n2,pz)`.
   Sospecho del redondeo en `hi16`/`mulu16` o de que `v`/`mag2` se calculan con un bit de
   diferencia. ¿Revisarías mi bloque?
   ```asm
   /* v */
   move.w 0(a5),d6
   ext.l  d6
   move.w d0,d5
   ext.l  d5
   muls.w d5,d6            /* n0*px */
   ... (n1*py, n2*pz, add.l)
   tst.l  d6
   bmi.w  .Lfv_back
   /* s = hi16(mag2), clamp 511 */
   move.w d0,d5
   muls.w d0,d5            /* px*px */
   move.w d1,d4
   muls.w d1,d4
   add.l  d4,d5
   move.w d7,d4
   muls.w d7,d4
   add.l  d4,d5            /* mag2 */
   swap   d5
   move.w d5,d4            /* s = hi16(mag2) */
   cmpi.w #511,d4
   bls.w  .Lfv_s_ok
   move.w #511,d4
   /* vv = hi16(v); res = mulu16(vv, invsqrt[s]) >> 16 */
   swap   d6
   move.w d6,d5            /* vv */
   add.w  d4,d4            /* s*2 */
   move.w 0(a4,d4.w),d4    /* invsqrt[s] */
   mulu.w d5,d4            /* d4 = vv * invsqrt[s] */
   swap   d4
   move.b d4,6(a5)         /* face->flags = res (byte bajo) */
   ```
   (El C++ equivalente: `mul16`/`mulu16` documentados en la consulta inicial.)

2. **¿Por qué el culling de `f2210` difiere** (v≈0)? El C++ usa `if (v >= 0)`; yo uso
   `tst.l d6; bmi.w .Lfv_back`. ¿Debe reproducirse algún borde exacto del original?

3. **Incluso con 17 aristas marcadas (m_angle=1000) el render sigue NEGRO.** ¿Puede ser tu
   hipótesis 2 (el dump es el estado del frame siguiente, no el que usa el draw)? Lo
   descarté volcando con `m_angle` **constante** (mismo estado cada frame). ¿Qué más mirarías
   — escrituras a `60(a3)` (M.z) o al bbox, o el orden `render` del engine?

**Repro:** `-DK_FLATSHADE_ASM=1` sobre `demos/amiga/116_flatshade_convex` (build/run de
`tools/`).
