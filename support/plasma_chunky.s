/* plasma_chunky: bucle caliente del plasma (copper chunky) en ASM m68k.
 *
 * Port fiel de la parte OPTIMIZADA de `UpdateChunky` de
 * demoscene-repo-orig/effects/plasma/plasma.c (el `#if OPTIMIZED` con el asm inline):
 *
 *     asm volatile("moveq #0,d0\n"
 *                  "moveb %1@(%2:w),d0\n"
 *                  "addb  %3@(%4:w),d0\n"
 *                  "addw  d0,d0\n"
 *                  "movew %5@(d0:w),%0@+\n"
 *                  "addql #2,%0\n"
 *                  : "+a" (ins)
 *                  : "a" (xbuf), "d" (x), "a" (ybuf), "d" (y), "a" (cmap)
 *                  : "d0");
 *
 * Se saca a .s porque g++ (GCC-15) mete un `andi.l #255` redundante por iteracion
 * (el `move.b` a registro de datos deja la parte alta sucia) y no emite `dbra`;
 * el bucle C quedaba en ~8 instrucciones contra las 6 del original, y en 68000 a
 * 7 MHz sobre RAM lenta eso domina el frame (2304 bloques por frame).
 *
 * IMPORTANTE: la iteracion va de `x = cols-1` a 0 (orden inverso del original): el
 * puntero `ins` avanza hacia delante, de modo que el bloque 0 recibe `xbuf[cols-1]`.
 *
 * Argumentos por MEMORIA en `g_plasma_chunky_args` (sin CFI, como fire_loop.s):
 *   [0] = dst   (u16*)  word `data` de la 1.ª instruccion COLOR00 de la fila
 *   [1] = xbuf  (u8*)   valores x por bloque (HTILES)
 *   [2] = yval  (u32)   `ybuf[y]`
 *   [3] = cmap  (u16*)  paleta RGB12 (256)
 *   [4] = cols  (u32)   HTILES (bloques por fila)
 *
 * Cada COLOR00 son 2 words (`[registro, data]`), de ahi el paso de 4 bytes: `(a0)+`
 * (2) + `addq.l #2` (2).
 */

	.section .text.plasma_chunky,"ax",@progbits
	.type plasma_chunky_row, function
	.globl	plasma_chunky_row

plasma_chunky_row:
	movem.l	d2-d3/a2-a3,-(sp)

	movea.l	g_plasma_chunky_args+0, a0	/* dst  (primera data COLOR00) */
	movea.l	g_plasma_chunky_args+4, a1	/* xbuf */
	move.l	g_plasma_chunky_args+8, d3	/* yval */
	movea.l	g_plasma_chunky_args+12, a2	/* cmap */
	move.l	g_plasma_chunky_args+16, d1	/* cols */
	subq.w	#1, d1				/* x = cols-1 (dbra) */

.Lplrow:
	moveq	#0, d0
	move.b	(a1,d1.w), d0			/* v = xbuf[x] */
	add.b	d3, d0				/* v += ybuf[y] */
	add.w	d0, d0				/* indice en bytes (word array) */
	move.w	(a2,d0.w), (a0)+		/* dst[0] = cmap[v]; dst += 2 bytes */
	addq.l	#2, a0				/* salta el word `registro` */
	dbra	d1, .Lplrow

	movem.l	sp@+, d2-d3/a2-a3
	rts
	.size	plasma_chunky_row, .-plasma_chunky_row
