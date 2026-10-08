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

## Lección de proceso (218): el coste no es un anexo — el modelo de contención es el diseño

Sesiones repetidas intentando cerrar la cadencia de la 218 («SPR Layer») contra su referencia
destaparon un patrón de fallo que no es de conocimiento del hardware sino de **modelo mental**:
se razona como si el Blitter/Copper/CPU fueran llamadas de un API que se ejecutan «cuando toca»
(emitir → esperar → listo), en vez de **recursos concurrentes que compiten por los mismos slots
de bus por scanline**. Consecuencias medidas en estos hilos:

- **«Sin esperas el cuerpo cabe en 1 campo»** era falso: quitando las esperas los jobs del
  Blitter se pisan entre sí (un juego de registros, BLTSIZE con el Blitter ocupado espera —
  waitingblits() en blitter.cpp) y el trabajo *no se hace*. El modelo correcto: la espera **es**
  tiempo de bus del Blitter, no sobrecoste eliminable. La suma de tiempos de pared reales
  (~230k ciclos ≈ 1,6 campos) es el suelo del diseño.
- **La cola de blits por IRQ en RAM lenta**: se encolaba con el Blitter en marcha y cada encolado
  costaba ~6k ciclos porque en un A500 la Slow RAM **comparte el bus con Agnus**. El modelo de
  mapa de memoria/contención (chip/slow/fast, quién arbitra) habría predicho el coste.
- **La IRQ BLIT dispara en *cada* finalización** (también de los blits síncronos) y su handler
  paga accesos a registros bajo DMA activo: sin presupuestarlo, el «encadenado sin esperas»
  añadió ~25 IRQs/update. Un handler debe salir por RAM (cola vacía) antes de tocar registros.
- **El Copper del efecto (~85-100 ciclos DMA/línea, ~45 % del bus) es el telón de fondo de todo
  blit**: cada job tarda ~20× su trabajo útil por el arbitraje. Tratarlo como «el efecto» y al
  Blitter como «mi trabajo» impide atribuir los tiempos de pared.
- **Tiras de BOBs (fusión de blits)**: diseño computacionalmente correcto pero invalidado por un
  detalle de registro (el barrel shifter arrastra bits entre celdas; AFWM/ALWM solo protegen
  los extremos de línea). Correcto ≠ suficiente.

Regla que queda: **antes de diseñar o medir, escribir el presupuesto** — slots de Copper por
línea, palabras × canales del Blitter, DMA de planos/sprites/audio, accesos a Chip del CPU — y el
**mapa de contención** (qué recurso usa cada acceso y qué más está activo en ese rango de raster).
Toda hipótesis lleva un coste predicho *antes* de medirse; si la medida contradice el modelo, se
corrige el modelo. El profiler por scanline (.amigaprofile) y la fuente del emulador
(fichero:línea) son la referencia, no los contadores caseros ni las correlaciones de pantalla.
