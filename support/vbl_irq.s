/* vbl_irq: trampoline de la interrupcion de VBlank (INTB_VERTB, nivel 3).
 *
 * Uso previsto (ver BACKGROUND_TASKS.md): la IRQ de VBlank es el **latido del juego**
 * (update/render con deadline de 1 frame) y el bucle principal es el **trabajo de
 * fondo** cooperativo, que la IRQ preempta. En 68000 (VBR=0) el autovector de nivel 3
 * esta en la direccion 0x6C.
 *
 * Al entrar: salva todos los registros, despacha a C++ (que limpia INTREQ y ejecuta el
 * tick del juego) y vuelve con RTE. Sin CFI (no generar .eh_frame no vacio).
 */
	.section .text.vbl_irq,"ax",@progbits
	.type	vbl_irq, function
	.globl	vbl_irq

vbl_irq:
	movem.l	d0-d7/a0-a6,-(sp)
	jsr	vbl_irq_dispatch
	movem.l	(sp)+,d0-d7/a0-a6
	rte
	.size	vbl_irq, .-vbl_irq
