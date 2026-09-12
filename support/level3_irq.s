/* level3_irq: handler UNICO del autovector de nivel 3 (0x6C en 68000 / VBR+0x6C).
 *
 * Nivel 3 lo comparten VERTB (VBlank), BLIT (blitter terminado) y COPER (copper):
 * una sola entrada debe leer INTREQR y despachar a cada servicio (limpiando su bit).
 * Instalarlo por separado (uno por fuente) pisaria el mismo vector.
 *
 * Al entrar: salva todos los registros, despacha a C++ y vuelve con RTE. Sin CFI
 * (no generar .eh_frame no vacio; ver fire_loop.s).
 */
	.section .text.level3_irq,"ax",@progbits
	.type	level3_irq, function
	.globl	level3_irq

level3_irq:
	movem.l	d0-d7/a0-a6,-(sp)
	jsr	level3_dispatch
	movem.l	(sp)+,d0-d7/a0-a6
	rte
	.size	level3_irq, .-level3_irq
