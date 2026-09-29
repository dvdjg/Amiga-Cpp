/* codec_asm.s: rutinas de descompresion de audio optimizadas para Motorola 68000.
 *
 * Port a GAS (`m68k-amiga-elf-as -mcpu=68000`) de los bucles calientes de los codecs de
 * `eng::audio`: Fibonacci Delta (IFF 8SVX), integracion delta (Delta+ZX0 / Delta+RLE) e
 * IMA ADPCM. Las implementaciones C++ (`fib_delta.hpp`, `ima_adpcm.hpp`, `pcm_codec.hpp`)
 * son la **referencia** y las que se usan en host y en builds sin ASM.
 *
 * ABI: la de este toolchain m68k GCC 15, que pasa **todos los argumentos por pila** (no por
 * registro; verificado compilando una llamada de prueba con `-S`). Dentro del callee, el
 * argumento i esta en `4 + 4*i (sp)` (tras la direccion de retorno). El retorno va en `d0`.
 * Se preservan los registros callee-saved que se usan (d2-d7/a2-a4).
 *
 *   eng::s32 eng_fib_delta_decode(const u8* src, usize len, u8* dst, const s8* table)
 *      4(sp)=src 8(sp)=len 12(sp)=dst 16(sp)=table  ->  d0 = 2*(len-2), o -1 si len < 3.
 *      `dst` debe tener 2*(len-2) bytes (el wrapper C++ comprueba el tamano).
 *
 *   void eng_delta_integrate(u8* buf, usize len)
 *      4(sp)=buf 8(sp)=len   (in-place; len <= 65536 por el contador `dbra`).
 *
 *   eng::s32 eng_ima_adpcm_decode(const u8* src, usize len, u8* dst,
 *                                 const s16* step_table, const s8* idx_table)
 *      4(sp)=src 8(sp)=len 12(sp)=dst 16(sp)=step 20(sp)=idx  ->  d0 = 2*(len-4), o -1.
 *
 * Sin `.cfi_*` (asm hoja sin unwind; igual que c2p_1x1_4.s). Equivalencia byte a byte con la
 * referencia C++ verificada por la demo 277 (gate en `g_eng_run_status.detail`: 0 = identico). */

	.section .text.eng_delta_integrate,"ax",@progbits
	.type eng_delta_integrate, function
	.globl eng_delta_integrate
eng_delta_integrate:
	move.l	4(sp), a0
	move.l	8(sp), d0
	tst.l	d0
	beq.s	.Ldi_ret
	moveq	#0, d1			/* acumulador (S_-1 = 0) */
	subq.l	#1, d0
.Ldi_loop:
	add.b	(a0), d1		/* S_n = S_(n-1) + D_n */
	move.b	d1, (a0)+
	dbra	d0, .Ldi_loop
.Ldi_ret:
	rts

	.section .text.eng_fib_delta_decode,"ax",@progbits
	.type eng_fib_delta_decode, function
	.globl eng_fib_delta_decode
eng_fib_delta_decode:
	movem.l	d2-d4/a2,-(sp)		/* 16 bytes: los args pasan a +16 */
	move.l	24(sp), d0		/* len */
	cmp.l	#3, d0
	blt.s	.Lfd_err
	move.l	20(sp), a0		/* src */
	move.l	28(sp), a1		/* dst */
	move.l	32(sp), a2		/* table */
	move.l	d0, d2
	subq.l	#2, d2			/* pares de nibbles */
	moveq	#0, d1
	move.b	1(a0), d1		/* x = semilla (src[1]) */
	addq.l	#2, a0
.Lfd_loop:
	moveq	#0, d4
	move.b	(a0)+, d4		/* par de nibbles, zero-extended */
	move.w	d4, d3
	lsr.w	#4, d4			/* nibble alto 0..15 */
	andi.w	#15, d3			/* nibble bajo 0..15 */
	add.b	(a2, d4.w), d1
	move.b	d1, (a1)+
	add.b	(a2, d3.w), d1
	move.b	d1, (a1)+
	subq.l	#1, d2
	bne.s	.Lfd_loop
	move.l	d0, d1
	subq.l	#2, d1
	add.l	d1, d1			/* muestras = 2*(len-2) */
	move.l	d1, d0
	movem.l	(sp)+, d2-d4/a2
	rts
.Lfd_err:
	moveq	#-1, d0
	movem.l	(sp)+, d2-d4/a2
	rts

	.section .text.eng_ima_adpcm_decode,"ax",@progbits
	.type eng_ima_adpcm_decode, function
	.globl eng_ima_adpcm_decode
eng_ima_adpcm_decode:
	movem.l	d2-d7/a2-a3,-(sp)	/* 32 bytes: los args pasan a +32 */
	move.l	40(sp), d0		/* len (arg1: 8+32) */
	cmp.l	#4, d0
	blt.s	.Lia_err
	move.l	36(sp), a0		/* src (4+32) */
	move.l	44(sp), a1		/* dst (12+32) */
	move.l	48(sp), a2		/* step table (16+32) */
	move.l	52(sp), a3		/* idx table (20+32) */
	move.l	d0, d7
	subq.l	#4, d7			/* pares de nibbles */
	moveq	#0, d2
	move.b	(a0), d2		/* step_index */
	cmp.w	#88, d2
	ble.s	.Lia_iok
	moveq	#88, d2
.Lia_iok:
	moveq	#0, d1
	move.b	2(a0), d1
	moveq	#0, d3
	move.b	3(a0), d3
	lsl.w	#8, d3
	or.w	d3, d1			/* pred = (s16)(src[2] | src[3]<<8) */
	ext.l	d1
	addq.l	#4, a0
.Lia_pair:
	moveq	#0, d4
	move.b	(a0)+, d4
	move.w	d4, d3
	lsr.w	#4, d3			/* codigo alto */
	andi.w	#15, d4			/* codigo bajo */
	move.w	d4, -(sp)		/* guarda el bajo (la subrutina clobbea d4/d5) */
	bsr.s	.Lia_nibble
	move.w	(sp)+, d3
	bsr.s	.Lia_nibble
	subq.l	#1, d7
	bne.s	.Lia_pair
	move.l	d0, d3
	subq.l	#4, d3
	add.l	d3, d3			/* muestras = 2*(len-4) */
	move.l	d3, d0
	movem.l	(sp)+, d2-d7/a2-a3
	rts
.Lia_err:
	moveq	#-1, d0
	movem.l	(sp)+, d2-d7/a2-a3
	rts

/* Un nibble: d3.w = codigo (0..15), d1.l = pred, d2.w = index. Actualiza pred/index y escribe
 * la muestra s8 (pred >> 8). Clobber: d4/d5/d6. */
.Lia_nibble:
	move.w	d2, d4
	add.w	d4, d4
	moveq	#0, d5
	move.w	(a2, d4.w), d5		/* step */
	move.l	d5, d6
	lsr.l	#3, d6
	btst	#0, d3
	beq.s	.Lia_b1
	move.l	d5, d4
	lsr.l	#2, d4
	add.l	d4, d6
.Lia_b1:
	btst	#1, d3
	beq.s	.Lia_b2
	move.l	d5, d4
	lsr.l	#1, d4
	add.l	d4, d6
.Lia_b2:
	btst	#2, d3
	beq.s	.Lia_b3
	add.l	d5, d6
.Lia_b3:
	btst	#3, d3
	beq.s	.Lia_pos
	sub.l	d6, d1
	bra.s	.Lia_clamp
.Lia_pos:
	add.l	d6, d1
.Lia_clamp:
	cmp.l	#32767, d1
	ble.s	.Lia_cl_lo
	move.l	#32767, d1
.Lia_cl_lo:
	cmp.l	#-32768, d1
	bge.s	.Lia_idx
	move.l	#-32768, d1
.Lia_idx:
	moveq	#0, d4
	move.b	(a3, d3.w), d4		/* delta de indice (s8) */
	ext.w	d4
	add.w	d4, d2
	cmp.w	#88, d2
	ble.s	.Lia_ix_lo
	moveq	#88, d2
.Lia_ix_lo:
	tst.w	d2
	bge.s	.Lia_out
	moveq	#0, d2
.Lia_out:
	move.w	d1, d6
	lsr.w	#8, d6
	move.b	d6, (a1)+
	rts
