| aplib_68000.s - descompresor aPLib para 68000 (156 bytes)
|
| Port a GAS del original vasm de Emmanuel Marty (`emmanuel-marty/apultra`,
| `asm/68000/unaplib_68000.S`, licencia zlib). Cambios: comentarios `;` -> `|`, las
| `lea N.w,An` (absoluto) por `move.l #N,An` (equivalente y sintaxis GAS segura) y las
| secciones/directivas de este repo. El codigo es identico.
|
|   in:  a0 = inicio de los datos comprimidos
|        a1 = inicio del buffer de salida
|   out: d0 = tamano descomprimido
|
| Copyright (C) 2020 Emmanuel Marty
| Con partes inspiradas por Franck "hitchhikr" Charlet
|
| This software is provided 'as-is', without any express or implied warranty. In no event
| will the authors be held liable for any damages arising from the use of this software.
| Permission is granted to anyone to use this software for any purpose, including commercial
| applications, and to alter it and redistribute it freely, subject to the following
| restrictions:
| 1. The origin of this software must not be misrepresented; you must not claim that you
|    wrote the original software. If you use this software in a product, an acknowledgment
|    in the product documentation would be appreciated but is not required.
| 2. Altered source versions must be plainly marked as such, and must not be misrepresented
|    as being the original software.
| 3. This notice may not be removed or altered from any source distribution.

| Envoltorio con ABI C (argumentos por pila, retorno en d0): evita el inline-asm por registros
| fijos, que dispara un ICE del gcc m68k 15.1 (ver docs/reference/toolchain/m68k-gcc.md 3.1).
|   eng::s32 eng_aplib_decompress(const u8* src, u8* dst)
	.section .text.eng_aplib_decompress,"ax",@progbits
	.type eng_aplib_decompress, function
	.globl eng_aplib_decompress
eng_aplib_decompress:
	move.l	8(sp),-(sp)	| guarda el inicio del destino (arg2)
	move.l	8(sp),a0	| src (arg1, desplazado por el push)
	move.l	12(sp),a1	| dst (arg2)
	jsr	apl_decompress
	move.l	(sp)+,d1	| recupera el inicio del destino
	move.l	a1,d0		| fin de salida
	sub.l	d1,d0		| tamano = fin - inicio
	rts

	.section .text.apl_decompress,"ax",@progbits
	.type apl_decompress, function
	.globl apl_decompress
apl_decompress:
	movem.l	a2-a6/d2-d3,-(sp)

	moveq	#-128,d1	| bit queue + bit to roll into carry
	move.l	#32000,a2	| constante de offset 32000
	move.l	#1280,a3	| constante de offset 1280
	move.l	#128,a4		| constante de offset 128
	move.l	a1,a5		| guarda el puntero de destino

.Lliteral:
	move.b	(a0)+,(a1)+	| copia byte literal
.Lafter_lit:
	moveq	#3,d2		| fija el flag LWM

.Lnext_token:
	bsr.s	.Lget_bit	| lee bit 'literal o match'
	bcc.s	.Lliteral	| si 0: literal

	bsr.s	.Lget_bit	| lee bit '8+n bits u otro tipo'
	bcs.s	.Lother_match	| si 11x: otro tipo de match

	bsr.s	.Lget_gamma2	| 10: lee los bits altos del offset (gamma2)
	sub.l	d2,d0		| offset alto == 2 cuando LWM == 3?
	bcc.s	.Lno_repmatch	| si no, no es rep-match

	bsr.s	.Lget_gamma2	| lee longitud del repmatch
	bra.s	.Lgot_len	| copia el match

.Lno_repmatch:
	lsl.l	#8,d0		| desplaza los bits altos del offset
	move.b	(a0)+,d0	| lee el byte bajo del offset
	move.l	d0,d3		| copia el offset a d3

	bsr.s	.Lget_gamma2	| lee la longitud del match
	cmp.l	a2,d3		| offset >= 32000?
	bge.s	.Linc_by_2	| si: +2 de longitud
	cmp.l	a3,d3		| offset >= 1280?
	bge.s	.Linc_by_1	| si: +1
	cmp.l	a4,d3		| offset < 128?
	bge.s	.Lgot_len	| si: +2
.Linc_by_2:
	addq.l	#1,d0
.Linc_by_1:
	addq.l	#1,d0

.Lgot_len:
	move.l	a1,a6		| direccion de backreference
	sub.l	d3,a6		| (destino - offset del match)
	subq.l	#1,d0		| dbf termina en -1, no en 0
.Lcopy_match:
	move.b	(a6)+,(a1)+	| copia byte del match
	dbf	d0,.Lcopy_match
	moveq	#2,d2		| limpia el flag LWM
	bra.s	.Lnext_token

.Lother_match:
	bsr.s	.Lget_bit	| lee bit 'match 7+1 o literal corto'
	bcs.s	.Lshort_match	| si 111: offset de 4 bits

	moveq	#1,d0		| 110: prepara longitud
	moveq	#0,d3		| limpia los bits altos del offset
	move.b	(a0)+,d3	| lee bits bajos del offset + bit de longitud
	lsr.b	#1,d3		| desplaza el offset, la longitud al carry
	beq.s	.Ldone		| comprueba EOD
	addx.b	d0,d0		| longitud = (1<<1)+carry, o sea 2 o 3
	bra.s	.Lgot_len

.Lshort_match:
	moveq	#0,d0		| limpia el offset corto
	bsr.s	.Lget_dibits	| lee un bit en d0 y otro en carry
	addx.b	d0,d0
	bsr.s	.Lget_dibits
	addx.b	d0,d0
	tst.b	d0		| offset cero?
	beq.s	.Lwrite_zero

	move.l	a1,a6		| direccion de backreference
	sub.l	d0,a6		| (destino - offset corto)
	move.b	(a6),d0		| lee el byte del match
.Lwrite_zero:
	move.b	d0,(a1)+	| escribe el byte o 0
	bra.s	.Lafter_lit

.Ldone:
	move.l	a1,d0		| puntero al final de la descompresion
	sub.l	a6,d0		| menos el inicio = tamano
	movem.l	(sp)+,a2-a6/d2-d3
	rts

.Lget_gamma2:
	moveq	#1,d0
.Lgamma2_loop:
	bsr.s	.Lget_dibits
	bcs.s	.Lgamma2_loop
	rts

.Lget_dibits:
	bsr.s	.Lget_bit
	addx.l	d0,d0
	| cae a .Lget_bit
.Lget_bit:
	add.b	d1,d1
	bne.s	.Lgot_bit
	move.b	(a0)+,d1
	addx.b	d1,d1
.Lgot_bit:
	rts
