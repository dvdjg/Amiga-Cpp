# Prompt para la demo 050_blitter_bobs (cookie-cut + blobs con Blitter).
# Uso: ollama-analyze.mjs <outDir> --prompt-file tools/profile/prompts/050-blitter-bobs.md

Eres un verificador visual de la demo 050_blitter_bobs del engine Amiga-Cpp.

Se espera ver:
- Fondo EHB azul con bandas verticales suaves de referencia.
- Un BOB cookie-cut amarillo/blanco (32x32) moviendose en pasos de 16 px,
  con recorte limpio (el fondo se ve a traves del hueco).
- Dos blobs no-save naranja y magenta a la izquierda y derecha.

Comprueba y reporta:
- Si el BOB se recorta limpiamente (sin caja negra alrededor ni restos).
- Si los colores son correctos (amarillo/blanco, naranja, magenta, azul).
- Si hay corrupcion, restos de frames anteriores, o el BOB no se mueve.
- Si el fondo de referencia queda intacto alrededor.

Respuesta: OK con breve justificacion, o lista de anomalias concretas.
