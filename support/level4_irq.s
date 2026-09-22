/* level4_irq: handler UNICO del autovector de nivel 4 (0x70 en 68000 / VBR+0x70).
 *
 * Nivel 4 lo comparten las cuatro voces de audio de Paula (AUD0..AUD3): una sola
 * entrada debe leer INTREQR y despachar al servicio (limpiando el bit que disparo).
 * Instalarlo por separado (uno por voz) pisaria el mismo vector. Es la via del
 * streaming digital: la IRQ avisa de que la voz agoto su buffer y toca cambiarlo.
 *
 * Al entrar: salva todos los registros, despacha a C++ y vuelve con RTE. Sin CFI
 * (no generar .eh_frame no vacio; ver fire_loop.s).
 */
	.section .text.level4_irq,"ax",@progbits
	.type	level4_irq, function
	.globl	level4_irq

level4_irq:
	movem.l	d0-d7/a0-a6,-(sp)
	jsr	level4_dispatch
	movem.l	(sp)+,d0-d7/a0-a6
	rte
	.size	level4_irq, .-level4_irq
