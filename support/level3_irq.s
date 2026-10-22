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

/* Entradas de los servidores de exec (AddIntServer) para el camino AmigaDOS.
 * Convenio de Exec: A1 = is_Data (scratch), D0/D1/A0/A1 scratch; D2-D7/A2-A6
 * preservados. El cuerpo C respeta el ABI, pero el stub guarda igualmente los
 * registros no scratch para blindar la cadena y, sobre todo, deja **A0 = $DFF000**
 * al retornar: los servidores de graphics/Intuition que van detrás esperan la base
 * de los registros custom en A0. Sin eso, un servidor de prioridad alta deja la
 * cadena VERTB sin despachar a los pocos frames (fallo de una llamada en CLI
 * cooperativo; ver consulta-ks13-cooperative-display-wait-en.md). */
	.section .text.exec_int_server,"ax",@progbits
	.type	eng_vbl_server_entry, function
	.globl	eng_vbl_server_entry
eng_vbl_server_entry:
	movem.l	d2-d7/a2-a6,-(sp)
	jsr	eng_vbl_server_body
	movem.l	(sp)+,d2-d7/a2-a6
	lea	0xdff000,a0
	moveq	#0,d0
	rts
	.size	eng_vbl_server_entry, .-eng_vbl_server_entry

	.type	eng_blit_server_entry, function
	.globl	eng_blit_server_entry
eng_blit_server_entry:
	movem.l	d2-d7/a2-a6,-(sp)
	jsr	eng_blit_server_body
	movem.l	(sp)+,d2-d7/a2-a6
	lea	0xdff000,a0
	moveq	#0,d0
	rts
	.size	eng_blit_server_entry, .-eng_blit_server_entry
