# HOST-395 — renderer de síntesis aditiva

Comprueba el renderer entero de una voz ACP1 aditiva: suma parciales armónicos en PCM8, aplica la forma de onda estándar y conserva la fase de la fundamental entre ventanas sucesivas sin usar `float`, heap ni Paula.

La prueba no demuestra todavía la reproducción Amiga ni la separación instrumental. Es la referencia host del núcleo de síntesis que usará el planificador de eventos ACP1 v3 antes de preparar buffers para `AUD1..AUD3` o para el mixer.
