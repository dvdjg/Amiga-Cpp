/* fire_asm.s — fuego (promedio de 4 vecinos de abajo) en asm 68000.
   Port del MainLoop de demoscene-repo-orig/effects/fire-rgb, para benchmark
   contra la version C++.

   ABI (argumentos por pila, como el resto de support/):
     fire_asm(u16* fire, u16 width, u16 height)
   Los argumentos se empujan de derecha a izquierda como long (4 bytes): el
   compilador emite `pea height; pea width; move.l fire,-(sp); jsr`. Tras
   `movem d2-d7/a2-a6,-(sp)` (11 registros = 44 bytes):
     sp@(48) = fire   (puntero, u16*)
     sp@(52) = width  (word baja del long)
     sp@(56) = height (word baja del long)

   Algoritmo (identico a la version C++ de la demo 063):
     de arriba (y=0) a abajo (y=H-3):
       fire[y][x] = (fire[y+2][x] + fire[y+1][x-1] + fire[y+1][x+1] + fire[y+1][x]) >> 2
   Usa punteros en registros y post-incremento (sin recalcular offsets y*W),
   que es la optimizacion clave frente al codigo que emite g++ para el bucle C++.
 */

	.section .text.fire_asm,"ax",@progbits
	.type fire_asm, function
	.globl	fire_asm
	.cfi_startproc

fire_asm:
	movem.l	d2-d7/a2-a6,-(sp)
	.cfi_adjust_cfa_offset 44

	move.l	sp@(48), a0	/* fire */
	move.l	sp@(52), d0	/* width (long, word baja = valor) */
	move.l	sp@(56), d1	/* height (long, word baja = valor) */

	/* d1 = contador del bucle exterior: `dbra` ejecuta d1+1 veces, así que
	   partimos de height-3 para recorrer exactamente height-2 filas (0 .. H-3). */
	subq.w	#3, d1

	/* d2 = W*2 (bytes por fila, u16 = 2 bytes) */
	move.w	d0, d2
	add.w	d2, d2

	/* a1 = A (fila y), a2 = C (fila y+1), a3 = E (fila y+2) */
	move.l	a0, a1
	move.l	a0, a2
	add.w	d2, a2		/* C = fire + W */
	move.l	a0, a3
	add.w	d2, a3
	add.w	d2, a3		/* E = fire + 2W */

	/* d3 = ancho interior = W - 2 (x de 1 a W-2) */
	move.w	d0, d3
	subq.w	#2, d3

.outer:
	/* a4 = A[x] (x=1), a5 = C[x], a6 = E[x] */
	move.l	a1, a4
	addq.w	#2, a4
	move.l	a2, a5
	addq.w	#2, a5
	move.l	a3, a6
	addq.w	#2, a6
	/* d4 = contador interior = (W-2) - 1 */
	move.w	d3, d4
	subq.w	#1, d4

.inner:
	moveq	#0, d5
	move.w	(a5), d5	/* C[x] */
	move.w	-2(a5), d6	/* C[x-1] */
	add.w	d6, d5
	move.w	2(a5), d6	/* C[x+1] */
	add.w	d6, d5
	move.w	(a6), d6	/* E[x] */
	add.w	d6, d5
	lsr.w	#2, d5
	move.w	d5, (a4)	/* A[x] = v */

	addq.w	#2, a4
	addq.w	#2, a5
	addq.w	#2, a6
	dbra	d4, .inner

	/* avanzar una fila */
	add.w	d2, a1
	add.w	d2, a2
	add.w	d2, a3
	dbra	d1, .outer

	movem.l	(sp)+, d2-d7/a2-a6
	.cfi_adjust_cfa_offset -48
	rts
	.cfi_endproc
	.size fire_asm, .-fire_asm
