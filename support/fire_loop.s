/* fire_loop: bucle de simulacion del fuego (fire-rgb) en ASM m68k.
 *
 * Port fiel del `MainLoop` de demoscene-repo-orig/effects/fire-rgb/fire-rgb.c, pero
 * como rutina .s aparte: g++ (GCC-15) **ignora los pins** `register asm("aN")` y no
 * puede asignar los 7 registros de direccion que exige el bucle original, asi que
 * mantenerlo como asm inline se cuelga/no compila. Sacandolo a gas se salta por
 * completo la asignacion de registros del compilador y se recupera la velocidad.
 *
 * Fuego: cada celda = calor. Cada iteracion lee 4 vecinos (B,C,D,E), los suma y
 * usa la tabla `dualtab` para obtener el color (a chunky) y el calor nuevo (a
 * `fire`). Truco del modo de direccionamiento m68k: `(dt, idx.w)` suma `idx` como
 * offset en BYTES; como `dualtab` es de uint32, `dt + idx` = `dt[idx/4]` -> el
 * indice es la MEDIA de los 4 vecinos (la suma / 4), no la suma.
 *
 * Argumentos por MEMORIA en un global `extern "C"` (g_fire_args), no por pila:
 *   [0] = chunky  (uint16_t*, salida: palabras de color HAM)
 *   [1] = fire    (uint32_t*, base del buffer de fuego = A)
 *   [2] = dt      (uint32_t*, tabla de color/calor = dualtab)
 *   [3] = iters   (nº de iteraciones externas; 4 FIREITER por iteracion)
 *
 * Ancho del fuego = 80 (constante del original): W-1=79, W=80, W+1=81, 2W=160
 * (en shorts; los punteros son uint32 -> offset en bytes = short*2).
 *
 * IMPORTANTE: nada de `.cfi_startproc`/`.cfi_adjust_cfa_offset` aqui. Generan una
 * seccion `.eh_frame` NO vacia; el canal lateral enumera las secciones del hunk
 * (text, rodata, .eh_frame, data, bss) pero el `.map` del runner filtra `.eh_frame`,
 * de modo que los indices se desplazan y `g_eng_run_status` se resuelve a una
 * direccion equivocada (la demo "no alcanza READY" aunque funcione). Sin CFI,
 * `.eh_frame` queda de 0 bytes y ambas listas coinciden.
 */

	.section .text.fire_loop,"ax",@progbits
	.type fire_loop, function
	.globl	fire_loop

fire_loop:
	movem.l	d2-d7/a2-a6,-(sp)

	/* Punteros por MEMORIA (g_fire_args): [0]=chunky [1]=fire [2]=dt [3]=iters. */
	movea.l	g_fire_args+0, a0
	movea.l	g_fire_args+4, a1
	movea.l	g_fire_args+8, a5
	move.l	g_fire_args+12, d7

	/* Punteros: A=a1 (fire+0), B=a2 (fire+W-1), C=a3 (fire+W),
	   D=a4 (fire+W+1), E=a6 (fire+2W). En bytes: (W-1)*2=158, 160, 162, 320. */
	lea	158(a1), a2
	lea	160(a1), a3
	lea	162(a1), a4
	lea	320(a1), a6

	move.w	d7, d7		/* iters en la word baja para dbra */
	subq.w	#1, d7		/* dbra: iters-1 */

.Lloop:
	.rept 4
	move.l	(a6)+, d0	/* E */
	add.l	(a2)+, d0	/* + B */
	add.l	(a4)+, d0	/* + D */
	add.l	(a3)+, d0	/* + C  -> d0 = suma de 4 vecinos */

	move.w	d0, d1		/* d1 = palabra baja de la suma */
	move.l	(a5, d1.w), d2	/* lo = dt[bytes d1] = dt[d1/4] (media) */
	swap	d0
	move.l	(a5, d0.w), d3	/* hi = dt[media del otro pixel] */

	move.w	d3, (a0)+	/* chunky: color de hi */
	move.w	d2, (a0)+	/* chunky: color de lo */

	swap	d2
	move.w	d2, d3		/* calor nuevo: high=hi.high, low=lo.high */
	move.l	d3, (a1)+	/* fire: realimentacion */
	.endr

	dbra	d7, .Lloop

	movem.l	sp@+, d2-d7/a2-a6
	rts
	.size	fire_loop, .-fire_loop
