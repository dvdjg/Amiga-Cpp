/* cia_irq: trampoline de la interrupcion de la CIA-A (nivel 2, autovector 0x68).
 *
 * La CIA-A afirma la linea IPL2 del 68000 (timers/FLAG/SDR; ver
 * amiga-bootcamp/01_hardware/common/cia_chips.md). Se usa para el motor de fondo
 * periodico (timer A) o como reloj de tiempo real (TOD). Al entrar: salva todos los
 * registros, despacha a C++ (que lee el ICR para reconocer y ejecuta el trozo de
 * fondo) y vuelve con RTE. Sin CFI (no generar .eh_frame no vacio).
 */
	.section .text.cia_irq,"ax",@progbits
	.type	cia_irq, function
	.globl	cia_irq

cia_irq:
	movem.l	d0-d7/a0-a6,-(sp)
	jsr	cia_dispatch
	movem.l	(sp)+,d0-d7/a0-a6
	rte
	.size	cia_irq, .-cia_irq
