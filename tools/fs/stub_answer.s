; Stub del modulo de prueba: answer() devuelve 42 (moveq #42,%d0 ; rts).
; Lo ensambla tools/fs/assemble.mjs con vasm (-Fbin) y make-volume.mjs lo empaqueta como
; .englib (con una celda relocable) y como HUNK nativo. Sustituye a los bytes a mano.
	moveq	#42,d0
	rts
