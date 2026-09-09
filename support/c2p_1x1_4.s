/* c2p_1x1_4: chunky 4bpp -> planar. Port a GAS del c2p de Kalms/Scout
   (demoscene-repo-orig/lib/libgfx/c2p_1x1_4.asm).
   Conversiones GAS:
     - comentarios en bloque (`,;` no es comentario en este as).
     - los lea con indice .l se sustituyen por move.l/add.l (equivalente en
       68000, porque GNU as exige 68020 para lea (An,Dn.L)).
   ABI: argumentos por pila. Tras movem d2-d7/a2-a6 (11 registros = 44 bytes):
     sp+48 = chunkyx (px, multiplo de 16)
     sp+52 = chunkyy (lineas)
     sp+56 = bplsize (bytes de salto entre planos consecutivos)
     sp+60 = chunkybuffer (entrada, 1 byte/pixel, nibble bajo)
     sp+64 = bitplanes (destino: 4 planos)
*/

	.section .text.c2p_1x1_4,"ax",@progbits
	.type c2p_1x1_4, function
	.globl	c2p_1x1_4
	.cfi_startproc

c2p_1x1_4:
	movem.l	d2-d7/a2-a6,-(sp)
	.cfi_adjust_cfa_offset 44

	/* Carga de los 5 argumentos por pila (ABI del repo). */
	move.l	sp@(48), d0	/* chunkyx (la rutina usa solo la word baja via lsr.w) */
	move.l	sp@(52), d1	/* chunkyy */
	move.l	sp@(56), d5	/* bplsize */
	move.l	sp@(60), a0	/* chunkybuffer */
	move.l	sp@(64), a1	/* bitplanes */

	lsr.w	#3, d0	/* chunkyx / 8 = bytes por fila de entrada */

	move.w	d1, d2
	mulu.w	d0, d2
	lsl.l	#3, d2	/* *8 -> offset al final del buffer de entrada */
	move.l	a0, a2
	add.l	d2, a2		/* a2 = puntero de parada (fin del chunky) */

	/* a3..a6 = punteros a los 4 planos del destino (separados por bplsize). */
	move.l	a1, a3		/* a3 = plano0 */
	move.l	a1, a4
	add.l	d5, a4		/* a4 = plano1 */
	add.l	d5, d5
	move.l	a1, a5
	add.l	d5, a5		/* a5 = plano2 */
	move.l	a4, a6
	add.l	d5, a6		/* a6 = plano3 */

	/* Constantes de mascara. */
	move.l	#0x0f0f0f0f, d4	/* separa nibbles */
	move.l	#0x00ff00ff, d5	/* separa bytes */

	move.l	(a0)+, d0
	move.l	(a0)+, d2
	move.l	(a0)+, d1
	move.l	(a0)+, d3

	and.l	d4, d0
	and.l	d4, d1
	and.l	d4, d2
	and.l	d4, d3
	lsl.l	#4, d0
	lsl.l	#4, d1
	or.l	d2, d0
	or.l	d3, d1

/* a3a2a1a0e3e2e1e0 b3b2b1b0f3f2f1f0 c3c2c1c0g3g2g1g0 d3d2d1d0h3h2h1h0
   i3i2i1i0m3m2m1m0 j3j2j1j0n3n2n1n0 k3k2k1k0o3o2o1o0 l3l2l1l0p3p2p1p0 */

	move.l	d1, d2
	lsr.l	#8, d2
	eor.l	d0, d2
	and.l	d5, d2
	eor.l	d2, d0
	lsl.l	#8, d2
	eor.l	d2, d1

/* a3a2a1a0e3e2e1e0 i3i2i1i0m3m2m1m0 c3c2c1c0g3g2g1g0 k3k2k1k0o3o2o1o0
   b3b2b1b0f3f2f1f0 j3j2j1j0n3n2n1n0 d3d2d1d0h3h2h1h0 l3l2l1l0p3p2p1p0 */

	move.l	d1, d2
	lsr.l	#1, d2
	eor.l	d0, d2
	and.l	#0x55555555, d2
	eor.l	d2, d0
	add.l	d2, d2
	eor.l	d2, d1

/* a3b3a1b1e3f3e1f1 i3j3i1j1m3n3m1n1 c3d3c1d1g3h3g1h1 k3l3k1l1o3p3o1p1
   a2b2a0b0e2f2f0f0 i2j2i0j0m2n2m0n0 c2d2c0d0g2h2g0h0 k2l2k0l0o2p2o0p0 */

	move.w	d1, d2
	move.w	d0, d1
	swap	d1
	move.w	d1, d0
	move.w	d2, d1

/* a3b3a1b1e3f3e1f1 i3j3i1j1m3n3m1n1 a2b2a0b0e2f2f0f0 i2j2i0j0m2n2m0n0
   c3d3c1d1g3h3g1h1 k3l3k1l1o3p3o1p1 c2d2c0d0g2h2g0h0 k2l2k0l0o2p2o0p0 */

	move.l	d1, d2

	bra.s	.start

.pix16:
	move.l	(a0)+, d0
	move.l	(a0)+, d2
	move.l	(a0)+, d1
	move.l	(a0)+, d3

	move.w	d6, (a5)+
	swap	d6

	and.l	d4, d0
	and.l	d4, d1
	and.l	d4, d2
	and.l	d4, d3
	lsl.l	#4, d0
	lsl.l	#4, d1
	or.l	d2, d0
	or.l	d3, d1

/* a3a2a1a0e3e2e1e0 b3b2b1b0f3f2f1f0 c3c2c1c0g3g2g1g0 d3d2d1d0h3h2h1h0
   i3i2i1i0m3m2m1m0 j3j2j1j0n3n2n1n0 k3k2k1k0o3o2o1o0 l3l2l1l0p3p2p1p0 */

	move.w	d7, (a3)+
	swap	d7

	move.l	d1, d2
	lsr.l	#8, d2
	eor.l	d0, d2
	and.l	d5, d2
	eor.l	d2, d0
	lsl.l	#8, d2
	eor.l	d2, d1

/* a3a2a1a0e3e2e1e0 i3i2i1i0m3m2m1m0 c3c2c1c0g3g2g1g0 k3k2k1k0o3o2o1o0
   b3b2b1b0f3f2f1f0 j3j2j1j0n3n2n1n0 d3d2d1d0h3h2h1h0 l3l2l1l0p3p2p1p0 */

	move.l	d1, d2
	lsr.l	#1, d2

	move.w	d6, (a6)+

	eor.l	d0, d2
	and.l	#0x55555555, d2
	eor.l	d2, d0
	add.l	d2, d2
	eor.l	d2, d1

/* a3b3a1b1e3f3e1f1 i3j3i1j1m3n3m1n1 c3d3c1d1g3h3g1h1 k3l3k1l1o3p3o1p1
   a2b2a0b0e2f2f0f0 i2j2i0j0m2n2m0n0 c2d2c0d0g2h2g0h0 k2l2k0l0o2p2o0p0 */

	move.w	d1, d2
	move.w	d0, d1
	swap	d1
	move.w	d1, d0
	move.w	d2, d1

/* a3b3a1b1e3f3e1f1 i3j3i1j1m3n3m1n1 a2b2a0b0e2f2f0f0 i2j2i0j0m2n2m0n0
   c3d3c1d1g3h3g1h1 k3l3k1l1o3p3o1p1 c2d2c0d0g2h2g0h0 k2l2k0l0o2p2o0p0 */

	move.l	d1, d2

.start:
	lsr.l	#2, d2
	eor.l	d0, d2
	and.l	#0x33333333, d2
	eor.l	d2, d0
	lsl.l	#2, d2
	eor.l	d2, d1

/* a3b3c3d3e3f3g3h3 i3j3k3l3m3n3o3p3 a2b2c2d2e2f2g2h2 i2j2k2l2m2n2o2p2
   a1b1c1d1e1f1g1h1 i1j1k1l1m1n1o1p1 a0b0c0d0e0f0g0h0 i0j0k0l0m0n0o0p0 */

	move.l	d0, d6
	move.l	d1, d7

	cmp.l	a0, a2
	bne.s	.pix16

	move.w	d6, (a5)+
	swap	d6
	move.w	d7, (a3)+
	swap	d7
	move.w	d6, (a6)+
	move.w	d7, (a4)+

	movem.l	(sp)+, d2-d7/a2-a6
	.cfi_adjust_cfa_offset -48
	rts
	.cfi_endproc
	.size c2p_1x1_4, .-c2p_1x1_4