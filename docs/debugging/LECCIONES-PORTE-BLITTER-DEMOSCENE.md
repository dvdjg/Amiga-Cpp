# Lecciones: porte del Blitter de `flatshade-convex` (demo 116)

Bitácora de un fallo de fidelidad al replicar por primera vez el efecto `flatshade-convex` del repo `demoscene-repo-orig`, y del proceso que lo resolvió. El objetivo no es narrar la historia, sino extraer reglas que eviten repetirlo al importar otros efectos.

## El síntoma

La réplica dibujaba la pelota con una **raya horizontal en cada vértice** de la silueta. El original, con la misma malla, la misma proyección y los mismos registros de lectura aparentes, salía limpio. La primera reacción fue descartar la técnica del original (`contorno ONEDOT+EOR` + **un** `area fill XOR`) y sustituirla por un relleno **por cara** con máscara + cookie-cut. Eso tapa el síntoma pero no lo explica y además **duplica el número de blits** (la ruta por cara es la lenta).

## La causa raíz

En el `DrawObject` del original, la macro de dibujo de línea es:

```c
custom_->bltcpt = (void *)bltcpt;   /* dirección calculada de la línea */
custom_->bltdpt = planes;           /* SIEMPRE la base del bitmap */
```

La réplica "limpió" esa asimetría: puso `bltcpt = bltdpt = <dirección calculada>`, porque en el resto de blits de copia ambos punteros suelen coincidir y `bltdpt = planes` parecía un descuido del autor.

No era un descuido. En **modo línea** (`LINEMODE`) el Blitter escribe el **primer píxel de la línea por el canal D** y el resto por **C**, y el minterm es **EOR**. Con `BLTDPTR` = dirección calculada, el primer píxel (que en una arista es un **vértice**) se escribe por D y por C y se **cancela** (`1 XOR 1 = 0`): la scanline pierde un cruce de contorno, la paridad *even-odd* del area fill se rompe y el relleno invierte el estado hacia el borde → **raya horizontal**. Con `BLTDPTR` = base, la escritura de D cae fuera de la figura y el vértice lo escribe solo C → contorno cerrado → relleno exacto.

La clave la aportó la referencia: **AHRM 3.ª, capítulo del Blitter, modo línea** — es el documento que describe el papel de D en el primer píxel y el de `FILL_XOR`/`FILL_OR`. También aclaró que en `BLTSIZE` el campo de **altura 0 = 1024 líneas** (y anchura 0 = 64 words), por lo que el original **sí** limpia y rellena los 4 planos contiguos con un único blit de altura 0; no era un no-op con desbordamiento accidental.

## Por qué falló teniendo el código fuente

1. **Se trató un valor de registro inusual como cosmético.** En código a nivel de registro, cada valor es semántico. "Normalizar" `bltdpt` para que coincidiera con `bltcpt` cambió el comportamiento. La regla es: si el original escribe un registro con un valor que no encaja con el patrón obvio, **no es ruido**; es un truco que hay que reproducir tal cual (y documentar por qué).
2. **No se consultó la referencia de hardware antes de portar.** El capítulo de modo línea del AHRM explica el canal D y el area fill; se portó por analogía con otras demos en vez de leer la especificación (viola la *regla de contexto técnico* de `AGENTS.md`).
3. **El gate de validación era débil.** El verificador de la demo comprobaba cobertura/tonos/centro, pero no detectaba **rayas horizontales** de 1 px dentro del objeto; y el modelo de visión local (`ollama`) confundía las facetas legítimas con rayas. Faltaba una métrica determinista (huecos internos por fila, racha horizontal máxima).
4. **Se aceptó una "diferencia intencionada" demasiado pronto.** Documentar la desviación ("la réplica sustituye el fill XOR por relleno por cara") como una decisión de diseño ocultó el bug en vez de forzar su diagnóstico.

## Prevención (checklist de importe)

- **Diff de registros 1:1.** Antes de dar por buena una réplica, enumerar los registros que escribe el original y compararlos con los de la réplica; marcar explícitamente cualquier diferencia. Un `BLTDPTR`/`BLTCMOD` distinto es un aviso, no un detalle.
- **Leer el AHRM del modo usado** (línea, area fill, cookie-cut, interleaved, HAM/EHB) *antes* de escribir el port. Citar la sección en el comentario del código.
- **Gate determinista para rasterizadores.** Al importar algo que dibuja píxeles, añadir una métrica de píxeles (huecos internos, racha máxima, fuga al borde) además de la métrica visual gruesa. La visión local (ollama) no basta para artefactos de 1 px.
- **Cuando una réplica se ve peor, investigar antes de desviar.** Una desviación que tapa un glitch debe quedar marcada como **pendiente de diagnóstico**, no como decisión final, hasta explicar el porqué del original.
- **Comparar contra el original por fase.** Capturar N frames alineados (`frame*8`) del original por `.adf` y de la réplica, y medir solape de máscara/cuadro; una diferencia de forma delata el problema.
