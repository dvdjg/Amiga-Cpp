| dzx0_68000.s - descompresor ZX0 para 68000 (88 bytes)
|
| Port a GAS del original vasm de Emmanuel Marty (`emmanuel-marty/unzx0_68000`,
| `unzx0_68000.S`, licencia zlib). Cambios: comentarios `;` -> `|` (el `;` no es comentario
| en GNU as m68k) y uso de la seccion/directivas de este repo. El codigo es identico.
|
|   in:  a0 = inicio de los datos comprimidos
|        a1 = inicio del buffer de salida (se avanza hasta el final)
|
| Copyright (C) 2021 Emmanuel Marty
| ZX0 compression (c) 2021 Einar Saukas, https://github.com/einar-saukas/ZX0
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

	.section .text.zx0_decompress,"ax",@progbits
	.type zx0_decompress, function
	.globl zx0_decompress
zx0_decompress:
	movem.l a2/d2,-(sp)	| preserve registers
	moveq	#-128,d1	| initialize empty bit queue plus bit to roll into carry
	moveq	#-1,d2		| initialize rep-offset to 1

.Lliterals:
	bsr.s	.Lget_elias	| read number of literals to copy
	subq.l	#1,d0		| dbf will loop until d0 is -1, not 0
.Lcopy_lits:
	move.b	(a0)+,(a1)+	| copy literal byte
	dbf	d0,.Lcopy_lits	| loop for all literal bytes

	add.b	d1,d1		| read 'match or rep-match' bit
	bcs.s	.Lget_offset	| if 1: read offset, if 0: rep-match

.Lrep_match:
	bsr.s	.Lget_elias	| read match length (starts at 1)
.Ldo_copy:
	subq.l	#1,d0		| dbf will loop until d0 is -1, not 0
.Ldo_copy_offs:
	move.l	a1,a2		| calculate backreference address
	add.l	d2,a2		| (dest + negative match offset)
.Lcopy_match:
	move.b	(a2)+,(a1)+	| copy matched byte
	dbf	d0,.Lcopy_match	| loop for all matched bytes

	add.b	d1,d1		| read 'literal or match' bit
	bcc.s	.Lliterals	| if 0: go copy literals

.Lget_offset:
	moveq	#-2,d0		| initialize value to $fe
	bsr.s	.Lelias_loop	| read high byte of match offset
	addq.b	#1,d0		| obtain negative offset high byte
	beq.s	.Ldone		| exit if EOD marker
	move.w	d0,d2		| transfer negative high byte into d2
	lsl.w	#8,d2		| shift it to make room for low byte

	moveq	#1,d0		| initialize length value to 1
	move.b	(a0)+,d2	| read low byte of offset + 1 bit of len
	asr.l	#1,d2		| shift len bit into carry/offset in place
	bcs.s	.Ldo_copy_offs	| if len bit is set, no need for more
	bsr.s	.Lelias_bt	| read rest of elias-encoded match length
	bra.s	.Ldo_copy_offs	| go copy match

.Lget_elias:
	moveq	#1,d0		| initialize value to 1
.Lelias_loop:
	add.b	d1,d1		| shift bit queue, high bit into carry
	bne.s	.Lgot_bit	| queue not empty, bits remain
	move.b	(a0)+,d1	| read 8 new bits
	addx.b	d1,d1		| shift bit queue, high bit into carry and shift 1 from carry
.Lgot_bit:
	bcs.s	.Lgot_elias	| done if control bit is 1
.Lelias_bt:
	add.b	d1,d1		| read data bit
	addx.l	d0,d0		| shift data bit into value in d0
	bra.s	.Lelias_loop	| keep reading

.Ldone:
	movem.l (sp)+,a2/d2	| restore preserved registers
.Lgot_elias:
	rts
