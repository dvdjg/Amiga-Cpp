# Seguimiento para grok — flatshade-convex ASM: aplicados tus fixes, sigue negro

Gracias por el análisis. Apliqué tus tres puntos y **avanzó mucho**, pero queda un fallo
que no consigo explicar. Adjunto estado exacto.

## Lo que apliqué (de tu respuesta)

1. **`zp` big-endian** — corregido: `move.w 2(sp),...` (word bajo del long) para
   `vertex.z`, y el divisor de `divs` explícito a 16 bits.
2. **`divs.w` con divisor palabra**: `move.w 2(sp),d1; divs.w d1,d0` (antes cargaba el long
   en `d2` y usaba su word bajo).
3. **Bucle exterior `do { while(...) } while (*group)`** replicado:
   `.Ltv_group_end: tst.w (a1); bne.w .Ltv_group`.

## Estado nuevo

- **Ya NO crashea** (antes acababa en el vector del Kickstart por un `movea.l g_fs_args`
  que cargaba el contenido; corregido a `lea g_fs_args`).
- **La salida de `fs_transform_vertices` COINCIDE EXACTAMENTE con la C++** con rotación
  fija (`m_angle=1000`), volcando el `objdat` nodo a nodo:

  ```
  n2   p=(597,1608,0)     v=(132,249,-3288)   (idéntico en C++ y ASM)
  n16  p=(184,1608,-568)  v=(171,236,-3700)
  n30  p=(-483,1608,-351) v=(0,0,0)
  n44  p=(-483,1608,351)  v=(161,16,-3649)
  n58  p=(184,1608,568)   v=(93,239,-3699)
  n72  p=(1171,1254,0)    v=(132,236,-2741)
  ```
  (`vertex.x/y/z` idénticos; también probado a `m_angle=0`.)

- **PERO el render sale NEGRO** (pantalla todo fondo; `verify-116`: «no hay balón»).
  Y no es que crashee: **la demo sigue viva** —
  `g_eng_run_status.state=3` (Ready), `frames` avanza 34→72→109→147 mientras la
  pantalla está negra.
- **Aislado**: ocurre igual con la ruta ASM completa (face+edge+transform) **y con solo el
  transform en ASM** (face/edge en C++). Es decir, en el caso aislado face/edge NO son la
  causa: el negro lo provoca el transform (o su efecto colateral), pese a que su salida
  coincide con la C++.

## El asm actual del transform (con tus fixes)

```asm
fs_transform_vertices:
	movem.l	d2-d7/a2-a6,-(sp)
	lea	-20(sp),sp			/* slots: zp/xp/yp/m0/m1 */
	lea	g_fs_args,a2
	movea.l	0(a2),a3			/* Object3D* */
	movea.l	0(a3),a0			/* objdat */
	movea.l	4(a3),a1			/* vertexGroups */
	movea.l	12(a2),a4			/* bbox */
	/* m0 = (M.x - normfx(m00*m01)) << 8 */
	move.w	44(a3),d0
	ext.l	d0
	move.w	38(a3),d1
	ext.l	d1
	move.w	40(a3),d2
	ext.l	d2
	muls.w	d2,d1
	moveq	#12,d2
	asr.l	d2,d1
	sub.l	d1,d0
	lsl.l	#8,d0
	move.l	d0,12(sp)			/* m0 */
	/* m1 = (M.y - normfx(m10*m11)) << 8 */
	move.w	52(a3),d0
	ext.l	d0
	move.w	46(a3),d1
	ext.l	d1
	move.w	48(a3),d2
	ext.l	d2
	muls.w	d2,d1
	moveq	#12,d2
	asr.l	d2,d1
	sub.l	d1,d0
	lsl.l	#8,d0
	move.l	d0,16(sp)			/* m1 */
	/* M.z = M.z - normfx(m20*m21) */
	move.w	60(a3),d0
	ext.l	d0
	move.w	54(a3),d1
	ext.l	d1
	move.w	56(a3),d2
	ext.l	d2
	muls.w	d2,d1
	moveq	#12,d2
	asr.l	d2,d1
	sub.l	d1,d0
	move.w	d0,60(a3)			/* M.z (s16) */
	/* bbox reset */
	move.w	#32767,0(a4)
	move.w	#-32768,2(a4)
	move.w	#32767,4(a4)
	move.w	#-32768,6(a4)
.Ltv_group:
	move.w	(a1)+,d1			/* i = *group++ */
	beq.w	.Ltv_group_end
	move.w	d1,d2
	subq.w	#2,d2
	lea	0(a0,d2.w),a5			/* node = objdat + i - 2 */
	tst.b	(a5)
	beq.w	.Ltv_group
	clr.w	(a5)				/* *pt++ = 0 (flags+pad) */
	move.w	2(a5),d3			/* x = point.x */
	move.w	4(a5),d4			/* y = point.y */
	move.w	6(a5),d5			/* z = point.z */
	move.w	d3,d6
	muls.w	d4,d6				/* xy = x*y */
	/* MULVERTEX1(xp, m0): t0=m00+y; t1=m01+x; t2=m02*z; xp=((t0*t1+t2-xy)>>4)+m0 */
	move.w	38(a3),d0
	add.w	d4,d0
	ext.l	d0
	move.w	40(a3),d7
	add.w	d3,d7
	ext.l	d7
	muls.w	d7,d0
	move.w	42(a3),d7
	ext.l	d7
	muls.w	d5,d7
	add.l	d7,d0
	sub.l	d6,d0
	asr.l	#4,d0
	add.l	12(sp),d0			/* + m0 */
	move.l	d0,4(sp)			/* xp */
	/* MULVERTEX1(yp, m1) : igual con m10,m11,m12 y +m1 */
	...
	move.l	d0,8(sp)			/* yp */
	/* MULVERTEX2(zp): t0=m20+y; t1=m21+x; t2=m22*z; t3=z; zp=normfx(...)+t3 */
	...
	move.l	d0,0(sp)			/* zp (long) */
	/* sx = div16(xp, (s16)zp) + 128 */
	move.w	2(sp),d1			/* zp como s16 (word bajo, big-endian) */
	move.l	4(sp),d0			/* xp */
	divs.w	d1,d0
	addi.w	#128,d0
	move.w	d0,8(a5)			/* vertex.x */
	/* sy = div16(yp, (s16)zp) + 128 */
	move.l	8(sp),d0			/* yp */
	divs.w	d1,d0
	addi.w	#128,d0
	move.w	d0,10(a5)			/* vertex.y */
	move.w	d1,12(a5)			/* vertex.z = (s16)zp */
	/* bbox */
	move.w	8(a5),d0
	cmp.w	0(a4),d0
	bge.w	.Ltv_bb0
	move.w	d0,0(a4)
.Ltv_bb0:
	... (misma logica para bx1/by0/by1)
	bra.w	.Ltv_group
.Ltv_group_end:
	tst.w	(a1)
	bne.w	.Ltv_group
.Ltv_done:
	lea	20(sp),sp
	movem.l	(sp)+,d2-d7/a2-a6
	rts
```

Contexto de la demo: pipeline de 3 buffers; por frame `draw_edges` (C++, usa
`vertex.x/y` y `edge->flags`) → `area_fill` → **face/edge/transform** (para el frame
siguiente). El display es 256×256×4 con copperlist.

## Preguntas nuevas

1. **Dado que el `objdat` (nodos) coincide con C++ y la demo no crashea, ¿qué efecto
   colateral del transform puede dejar la pantalla NEGRA?** Sospechas: (a) el
   `movem`/`lea -20(sp),sp` deja el stack o un registro distinto de lo que el render C++
   espera (¿`a6`? ¿`d6/d7`?); (b) `lea g_fs_args,a2` / `movea.l 0(a2),a3` leen mal el
   struct en algún caso; (c) la escritura `move.w d0,60(a3)` corrompe algo; (d) el
   `clr.w (a5)` o los stores de `vertex` pisan memoria vecina (¿el `pad`? ¿el nodo
   siguiente?).
2. **¿Puede ser dependiente del ángulo?** El dump fue a un único `m_angle`. Si en algún
   ángulo `|xp/zp|` no cabe en s16, `divs.w` pone V y el cociente es indefinido (tú lo
   mencionaste). ¿Recomiendas calcular `xp/yp/zp` y comprobar `V` en varios ángulos, o
   instrumentar `0xB70000` para volcar `xp/yp/zp` intermedios?
3. **¿El orden del pipeline importa?** El `transform` corre DESPUÉS de `draw_edges` (prepara
   el frame siguiente). Si el ASM pisa algún registro que el C++ usa al volver del call,
   ¿dónde mirarías primero?
4. **Hipótesis Blitter/copper**: ¿puede el transform (que no toca el Blitter) dejar el
   estado del Blitter/copper inconsistente por timing (p. ej. por ser más rápido/lento que
   la versión C++ y cambiar cuándo se lanza el pre-clear del siguiente frame)?

Repro: `bash ./tools/build/build-demo.sh demos/amiga/116_flatshade_convex --debug --clean`
y `bash ./tools/run/run-demo.sh demos/amiga/116_flatshade_convex` con
`-DK_FLATSHADE_ASM=1`.
