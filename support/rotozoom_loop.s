/* rotozoom_loop: bucle interior del rotozoom (eng/graphics/effects/rotozoom.hpp).
 *
 * Se saca a gas por el mismo motivo que `fire_loop.s`: GCC-15 no alcanza el codegen
 * apretado y el bucle C++ resultante cuesta ~152 ciclos/pixel en 68000 (medido en la
 * demo 061). Se conserva la version C++ canonica en el header; este `.s` debe producir
 * EXACTAMENTE el mismo buffer (el gate de la demo lo comprueba).
 *
 * Truco de velocidad: el indice de textura es
 *     idx = ((u>>16)&63)<<6 | ((v>>16)&63)
 * y `<<6` costaba 18 ciclos por pixel (`lsl.w #6`). Si se mantiene `u` **pre-escalado**
 * (`U = u<<6`), entonces `texel_u<<6` ya vive en los bits 6..11 de la word alta de U y
 * basta `swap` + `and.w #0xFC0`: un solo `and` en vez de `and`+`lsl`. El escalado de 6
 * bits no pierde precision porque el acumulador es de 32 bits (el paso `du<<6` es exacto)
 * y solo importan los bits 6..27 de U (fraccion + 6 bits de texel).
 *
 * Especializado a textura **64x64** (mascara 63, shift 6), como `fire_loop` esta
 * especializado a ancho 80: es el unico consumidor. La via C++ sigue siendo parametrica.
 *
 * Precaucion de desbordamiento: dentro de una media fila, `|ΔU| <= (w/2)*|du<<6|`; con
 * `w=320` y el zoom de la demo (<=2.0) son <=1.34e9, y el inicio esta enmascarado a 28
 * bits (<2.7e8), asi que nunca se cruza el bit 31. Para zooms mayores habria que bajar
 * el tamano de media fila o volver a la via C++.
 *
 * Argumentos por MEMORIA en `g_rotozoom_args`:
 *   [0]  dst    (u8*)  salida chunky (w*h bytes)
 *   [1]  tex    (u8*)  textura 64x64 (4096 bytes)
 *   [2]  w      (u32)  pixeles por fila (par)
 *   [3]  h      (u32)  filas
 *   [4]  U      (s32)  u del pixel (0,0) << 6  — ENTRADA Y SALIDA (avanza por fila)
 *   [5]  V      (s32)  v del pixel (0,0)       — ENTRADA Y SALIDA
 *   [6]  du6    (s32)  paso de u por pixel << 6
 *   [7]  dv     (s32)  paso de v por pixel
 *   [8]  ru     (s32)  avance del inicio de fila en U (= dv<<6)
 *   [9]  rv     (s32)  avance del inicio de fila en V (= -du)
 *   [10] half   (u32)  (w/2)-1  (contador de media fila, para la re-mascara)
 *
 * Nada de `.cfi_*`: generan `.eh_frame` no vacio y desajustan la enumeracion de
 * secciones del canal lateral respecto al `.map` (ver fire_loop.s).
 */

	.section .text.rotozoom_loop,"ax",@progbits
	.type rotozoom_loop, function
	.globl	rotozoom_loop

rotozoom_loop:
	movem.l	d2-d7,-(sp)

	movea.l	g_rotozoom_args+0, a1	/* dst */
	movea.l	g_rotozoom_args+4, a0	/* tex */
	move.l	g_rotozoom_args+24, d4	/* du6 */
	move.l	g_rotozoom_args+28, d5	/* dv  */
	move.l	g_rotozoom_args+12, d7	/* h */
	subq.w	#1, d7

.roto_row:
	move.l	g_rotozoom_args+16, d0	/* inicio de fila en U (<<6) */
	and.l	#0x0FFFFFFF, d0
	move.l	g_rotozoom_args+20, d1	/* inicio de fila en V */
	and.l	#0x0FFFFFFF, d1
	move.l	g_rotozoom_args+40, d6	/* (w/2)-1 */
.roto_h1:
	move.l	d0, d2
	swap	d2
	and.w	#0xFC0, d2		/* texel_u<<6 (ya pre-escalado) */
	move.l	d1, d3
	swap	d3
	and.w	#63, d3
	or.w	d3, d2
	move.b	(a0,d2.w), d3
	move.b	d3, (a1)+
	add.l	d4, d0
	add.l	d5, d1
	dbra	d6, .roto_h1

	and.l	#0x0FFFFFFF, d0		/* re-mascara a media fila (overflow) */
	and.l	#0x0FFFFFFF, d1
	move.l	g_rotozoom_args+40, d6
.roto_h2:
	move.l	d0, d2
	swap	d2
	and.w	#0xFC0, d2
	move.l	d1, d3
	swap	d3
	and.w	#63, d3
	or.w	d3, d2
	move.b	(a0,d2.w), d3
	move.b	d3, (a1)+
	add.l	d4, d0
	add.l	d5, d1
	dbra	d6, .roto_h2

	/* Avance de los inicios de fila (en memoria): U -= dv<<6 ; V -= -du. */
	move.l	g_rotozoom_args+16, d2
	sub.l	g_rotozoom_args+32, d2
	move.l	d2, g_rotozoom_args+16
	move.l	g_rotozoom_args+20, d3
	sub.l	g_rotozoom_args+36, d3
	move.l	d3, g_rotozoom_args+20

	dbra	d7, .roto_row

	movem.l	(sp)+, d2-d7
	rts
	.size rotozoom_loop, .-rotozoom_loop
