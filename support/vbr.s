/* vbr.s — valor del VBR (base de vectores de excepción) para los reproductores
   de música que lo referencian (ptplayer usa `_ExcVecBase`). En 68000 el VBR
   es 0 (los vectores viven en la dirección 0). En 68010+ se podría reubicar,
   pero el target del engine es 68000. */
	.section .data._ExcVecBase,"aw",@progbits
	.globl	_ExcVecBase
_ExcVecBase:
	.long	0
