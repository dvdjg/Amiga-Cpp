/* rotozoom_loop: bucle interior del rotozoom (eng/graphics/effects/rotozoom.hpp).
 *
 * Se saca a gas por el mismo motivo que `fire_loop.s`: GCC-15 no alcanza el codegen
 * apretado y el bucle C++ resultante cuesta ~152 ciclos/pixel en 68000 (medido en la
 * demo 061), frente a los ~100 de esta version con los registros asignados a mano.
 *
 * Se conserva la version C++ canonica en el header; este `.s` debe producir el MISMO
 * resultado (el gate de la demo 061 es el que lo comprueba, aunque sea por color).
 *
 * Especializado a textura **64x64** (mascara 63, shift 6), igual que `fire_loop` esta
 * especializado a ancho 80: es el unico consumidor. La via C++ sigue siendo parametrica.
 *
 * Argumentos por MEMORIA en `g_rotozoom_args`, como `fire_loop.s`:
 *   [0] dst  (u8*)  salida chunky (w*h bytes)
 *   [1] tex  (u8*)  textura 64x64 (4096 bytes)
 *   [2] w    (u32)  pixeles por fila
 *   [3] h    (u32)  filas
 *   [4] u    (s32)  (u,v) del pixel (0,0), 16.16
 *   [5] v    (s32)
 *   [6] du   (s32)  pasos por pixel, 16.16
 *   [7] dv   (s32)
 *   [8] adv_u(s32)  correccion del acumulador al cerrar fila (ver rotozoom_steps)
 *   [9] adv_v(s32)
 *
 * Mapeo (identico a la version C++): idx = ((u>>16)&63)<<6 | ((v>>16)&63);
 * dst = tex[idx]; u += du; v += dv; al cambiar de fila: u -= dv, v += du.
 *
 * Nada de `.cfi_*`: generan `.eh_frame` no vacio y desajustan la enumeracion de
 * secciones del canal lateral respecto al `.map` (ver fire_loop.s y
 * docs/debugging/HISTORIAL-CAMBIOS.md).
 */

	.section .text.rotozoom_loop,"ax",@progbits
	.type rotozoom_loop, function
	.globl	rotozoom_loop

rotozoom_loop:
	movem.l	d2-d7,-(sp)

	movea.l	g_rotozoom_args+0, a1	/* dst */
	movea.l	g_rotozoom_args+4, a0	/* tex */
	move.l	g_rotozoom_args+16, d0	/* u  */
	move.l	g_rotozoom_args+20, d1	/* v  */
	move.l	g_rotozoom_args+24, d4	/* du */
	move.l	g_rotozoom_args+28, d5	/* dv */

	move.l	g_rotozoom_args+8, d6	/* w */
	move.l	g_rotozoom_args+12, d7	/* h */
	subq.w	#1, d6
	subq.w	#1, d7

.roto_row:
.roto_px:
	/* Extraccion del indice: `swap` deja la word alta en la baja (barato); el
	   desplazamiento de 6 solo hace falta en u (v ocupa los bits bajos). */
	move.l	d0, d2
	swap	d2
	and.w	#63, d2
	lsl.w	#6, d2
	move.l	d1, d3
	swap	d3
	and.w	#63, d3
	or.w	d3, d2

	move.b	(a0,d2.w), d3	/* texel */
	move.b	d3, (a1)+

	add.l	d4, d0
	add.l	d5, d1
	dbra	d6, .roto_px

	move.l	g_rotozoom_args+8, d6	/* la fila siguiente reusa el contador */
	subq.w	#1, d6

	/* El bucle deja el acumulador AL FINAL de la fila (inicio + w*paso); hay que
	   devolverlo al arranque de la fila siguiente (inicio -/+ paso de fila). */
	sub.l	g_rotozoom_args+32, d0
	sub.l	g_rotozoom_args+36, d1

	dbra	d7, .roto_row

	movem.l	(sp)+, d2-d7
	rts
	.size rotozoom_loop, .-rotozoom_loop
