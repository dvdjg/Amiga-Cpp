# BOBs sobre fondos animados: carreras con el haz y planificación segura

Ficha de técnica para el problema de **flicker/tearing al repintar BOBs en un único framebuffer**: el Blitter escribe el mismo bitmap que Denise está leyendo línea a línea, y si el haz de la CRT pasa por la zona del BOB mientras el Blit sigue en curso, esa línea se muestra a medias (el césped nuevo con el fuego viejo, o al revés). No es un defecto de la demo 207: afecta a cualquier BOB, capa de objetos o cobertura que se pinte sobre un fondo vivo en un solo buffer.

La conclusión práctica del AHRM es explícita: la animación por restaurar/redibujar «works best with beam-synchronized blits or double buffering» (AHRM 3.ª, `docs/reference/ahrm/Amiga Hardware Reference Manual 3rd edition (Commodore Amiga Inc.) (Z-Library).cat.md:5159`). Hay varias formas de conseguir esa sincronización; esta ficha ordena las opciones y concreta un algoritmo barato para el engine.

## 1. Por qué ocurre

El haz recorre la pantalla de arriba abajo, una línea cada vez (AHRM:1124); el Blitter arranca al escribir `BLTSIZE` y sigue trabajando de forma asíncrona mientras la CPU continúa (AHRM:4988; en WinUAE el registro se atiende en `../WinUAE-DBG/custom.cpp:3982` y `:7355`). La display DMA lee los bitplanes de la misma Chip RAM que el Blitter está escribiendo: no hay doble buffer ni handshake entre ambos. El único dato que dice dónde va el haz es el contador de posición, legible por CPU (`VPOSR`/`VHPOSR`, AHRM:6397–6412) y por el Copper (`WAIT`/`SKIP`, AHRM:579–623); en el emulador, `VPOSR()` devuelve la posición viva en `../WinUAE-DBG/custom.cpp:2429`. Por eso el fallo es intermitente: depende de la línea del haz y del instante en que el Blitter escribe cada palabra.

## 2. Opciones

| Estrategia | Qué hace | Coste | Cuándo |
|---|---|---|---|
| **Doble buffer completo** | Se dibuja en el buffer oculto y se presenta en VBlank | 2× bitmap (en 207, 256 KB el ring); ya existe como `DEMO_DOUBLE_BUFFER` | Si la memoria lo permite; es la única que elimina el 100 % de las carreras |
| **Doble buffer por banda + Copper** | Solo la banda de BOBs tiene dos copias; el Copper cambia `BPLxPT` en el borde de la banda | 2× banda (no 2× pantalla) + Copper | BOBs concentrados en una franja vertical; el resto del fondo puede seguir en un buffer |
| **Scheduler por deadlines de haz** | Ordena los jobs por `y`; antes de cada lote lee `VPOSR`; ejecuta solo si el Blitter termina antes de que el haz llegue o si el haz ya pasó; si no, difiere | 1 lectura de `VPOSR` por lote + bucketing por `y`; puede bajar la cadencia de ese BOB | **Recomendado como camino general del engine**: no gasta memoria y convierte el tearing en latencia ocasional |
| **Blits disparados por Copper** | La CPU precarga los registros; el Copper espera la línea inferior del BOB (con BFD para esperar al Blitter) y escribe `BLTSIZE`; el Blit corre por detrás del haz | Lista de Copper con un `WAIT` por BOB; +1 frame de latencia | BOBs críticos y pocos; el AHRM advierte que, al encadenar Blits desde el Copper, hay que esperar la señal de blitter-finished (AHRM:1013) |
| **Sprites hardware** | El sprite se fetcha por línea desde su estructura DMA; no escribe el bitmap | 16 colores por par, 8 canales, rearme por Copper; límite de tamaño | Objetos pequeños (copas, props) con paleta compatible |
| **Solo VBlank** | Todo el trabajo de BOBs cabe en el intervalo de VBlank | 0 si cabe; si no, el frame se parte | Solo si el presupuesto de Blitter cabe; en 207 el parche de la fogata no cabe |

## 3. Algoritmo por deadlines (recomendado)

La idea es no dibujar nunca un BOB mientras el haz está dentro de su caja vertical. Se conocen `y_top`/`y_bottom` del destino de cada job y se puede leer `VPOSR` entre lotes pequeños; el coste es una lectura de registro por lote, no por píxel.

```text
por frame:
  jobs = BOBs visibles (restore + draw atómicos por celda/grupo)
  buckets = agrupar jobs por y_top (o por y_bottom)
  para cada bucket en orden ascendente de y:
    vpos = VPOSR                     # línea actual del haz
    si vpos >= bucket.bottom + margen:
      ejecutar todos los jobs del bucket   # el haz ya pasó; se verá el próximo frame
    si no, si vpos + lineas_que_tarda_el_blit + margen < bucket.top:
      ejecutar todos los jobs del bucket   # termina antes de que el haz llegue
    si no:
      diferir el bucket al siguiente frame # el arte anterior se queda entero; no se parte
```

Propiedades:

- **No hay tearing**: un job solo se lanza si el haz está por debajo de su caja (se verá en el siguiente frame) o si el Blitter termina antes de que el haz llegue a `y_top`. En el caso límite se difiere; el BOB muestra el frame anterior completo.
- **Coste**: O(n) para llenar los buckets (el `y` del destino ya está en el job), una lectura `VPOSR` y una comparación por bucket; los buckets vacíos se saltan. No hay ordenación por frame si los BOBs están en un array con `y` estable; si se mueven, un sort O(n log n) pequeño o un bucket por línea.
- **El margen** cubre la contención del bus (display DMA, Copper, audio y refresco; prioridades en AHRM:5694), el coste de los canales del Blitter y la incertidumbre de la línea. Se calibra con el peor caso medido, no con el promedio.
- **`lineas_que_tarda_el_blit`** se estima con las palabras del job (`words_per_row × height × planos/canales`) y el coste por palabra del Blitter, con el margen. Para el `BlitJob` del engine se puede cachear por tipo/tamaño; no hace falta medir cada job.
- **Atomicidad**: el restore y el draw de una misma celda van en el mismo bucket y no se separan; el scheduler difiere el grupo entero (el parche ya trabaja así con `pass_deferred`). La prioridad nunca reordena restore/draw de la misma celda.
- **Prioridad**: si el frame no cabe, se difieren primero los buckets con `y` más bajo (deadline más temprano) o los de capas decorativas; la política de capas del 207 (fogata > flores > agua) encaja aquí. Un BOB diferido no se congela para siempre: el bucket se reintenta en el siguiente frame.

La variante **Copper** cambia «leer `VPOSR` y decidir» por «el Copper espera la línea inferior del BOB y dispara el Blit». La ventaja es que no hay estimación: cuando el `WAIT` se cumple, el haz ya pasó el BOB; el Blit se ve en el siguiente frame. El coste es una lista de Copper por BOB y la coordinación con la señal de blitter-finished (AHRM:1013: no se deben cambiar registros del Blitter mientras trabaja). Es la opción natural si el engine ya encadena blits por IRQ (`execute_frame_plan_async`).

## 4. Doble buffer por banda

Si los BOBs se concentran en una franja (por ejemplo, la capa de objetos de un juego de plataformas), se puede duplicar solo esa franja y dejar el fondo en un único buffer:

```text
buffer A: fondo completo + banda A de BOBs
buffer B: solo la banda B de BOBs
Copper: arriba -> BPLxPT = fondo; en la banda -> BPLxPT = A o B (alternando cada frame)
```

El Copper cambia los punteros de plano en el borde superior de la banda; el resto de la pantalla no se duplica. El coste es `2 × ancho × alto_banda × planos / 8` y una copperlist con los `BPLxPT` de la banda. El precio es que la banda debe ser contigua y que el scroll vertical de la banda obliga a mover/reciclar sus dos copias. En 207 la capa de objetos y el agua ocupan casi toda la pantalla, así que la banda sería demasiado grande; el scheduler es más razonable allí.

## 5. Sprites y objetos pequeños

Un sprite no escribe el bitmap: Denise lo fetcha de su estructura DMA al principio de cada línea y lo mezcla con el playfield (AHRM:3765). Para objetos de 4/16 colores (copas de árbol, props pequeños) es la vía con menos carrera, siempre que la actualización de `SPRxPOS`/`SPRxCTL`/`SPRxPT` se haga en VBlank y el rearme esté coordinado con el haz (el engine ya tiene `SpriteLineLayer` y rearme por Copper). Los BOBs de 6 planos EHB de 207 no caben en un sprite de 16 colores; la capa de objetos de la demo sí podría dividirse por paleta si el arte lo permite.

## 6. Encaje en el engine

- **El scheduler pertenece al render, no a la lógica de juego.** La lógica no sondea `VPOSR`; el `FramePlan`/present expone una política que lee el haz entre lotes. Es la misma frontera que la regla de no escribir `INTENA`/`VPOSR` desde la demo: el engine puede leer el reloj de vídeo, la demo no.
- **Los jobs ya tienen el destino.** `BlitJob` lleva `destination`, `words_per_row`, `height` y `bitplane_count`; el `y` de destino se deriva del `BlitPtr`/`BobTarget`. Los `MapSprite`/`Bob`/`TileAnimator` pueden aportar su caja (`y_top`/`y_bottom`) al plan para que el scheduler no la recalcule.
- **Fases**: (1) ordenar/bucketear los jobs de BOB por `y`; (2) leer `VPOSR` entre lotes y aplicar la condición de seguridad; (3) telemetría (`jobs_deferred_by_beam`, `max_lateness`) y un test sintético con un BOB en la línea del haz; (4) la variante Copper para los BOBs críticos; (5) la banda doble si la memoria lo justifica.
- **El doble buffer de 207** ya existe como variante (`DEMO_DOUBLE_BUFFER`, 256 KB) y no compensa en el perfil actual; el scheduler no lo sustituye, pero permite dejar el buffer único con latencia ocasional en vez de tearing.

### Implementación actual (engine)

El camino común ya está en el engine, sin API nueva para los BOBs:

- `BlitJob` lleva `bob`, `bob_top` y `bob_bottom` (líneas de pantalla). `bob_detail::mark_bob` los fija desde `bob_draw`, `bob_erase`, `bob_save_box`, `bob_restore_box`, `bob_draw_interleaved_pair`, `MapSpriteCache::emit_copy` y el `TileAnimator` (`mark_bob_screen`, con el `display_offset` del anillo).
- `AmigaBackend::wait_bob_beam(plan)` recorre los jobs marcados **una vez por plan**, calcula la caja común (deadline superior más temprano, borde inferior más bajo) y el coste en palabras, y solo espera si el haz está dentro o si el lote no cabe antes del deadline. Es una lectura de `VPOSR` (`current_raster_line`) y un giro corto; el coste medido en 207 es ~0,4 fps en la visita. Los jobs de scroll/tiles no se marcan y no esperan.
- La espera ocurre también en el camino **async** (`execute_frame_plan_async`) antes de lanzar la cadena.
- **Todos los BOBs del plan** (mapa, objetos, actores) comparten la guarda; los sprites hardware no escriben el bitmap y no la necesitan. La actualización de un BOB que el haz ya pasó se ve en el frame siguiente; si el lote cabía antes del deadline se dibuja en el frame actual.
- Medido: 207 visita **39,54 fps** (frente a 39,90 sin guarda) con el fuego, flores/agua y objetos protegidos; 208 (objetos sobre agua animada) arranca sin tearing aparente a **26,50 fps**. La guarda gruesa por plan es la versión más barata; el refinamiento por bucket/Copper queda para cuando el perfil lo pida.

## 7. Qué medir

- Secuencia de capturas con el BOB en la misma línea que el haz (un BOB a `y` fijo, cámara quieta) y el `frame-diff` de la zona: el scheduler debe dar 0 píxeles a medio actualizar; el frame diferido se reconoce por el arte anterior completo.
- `VPOSR` leído en el emulador y en hardware; la línea del BOB; el margen; el peor caso de palabras.
- Coste: una lectura de `VPOSR` + comparación por bucket; el número de jobs diferidos y la edad máxima. No debe superar el 1 % del frame.
- La prueba de aceptación es visual y de cadencia: sin tearing y sin un BOB congelado más de N frames; el `flicker`/`frame-diff` del repo.

## 8. Fuentes

- AHRM 3.ª, copia local: `docs/reference/ahrm/Amiga Hardware Reference Manual 3rd edition (Commodore Amiga Inc.) (Z-Library).cat.md` — beam-synchronized blits/double buffering (:5159), beam top-to-bottom (:1124), Copper WAIT/SKIP (:579–623), BFD/Copper+Blitter (:1013), prioridades DMA (:5694), `VPOSR`/`VHPOSR` (:6397–6412), arranque del Blitter por `BLTSIZE` (:4988).
- WinUAE-DBG: `../WinUAE-DBG/custom.cpp:2429` (`VPOSR`), `:3982`/`:7355` (`BLTSIZE`), `../WinUAE-DBG/blitter.cpp:1993` (inicio del Blitter); el render por línea está en `../WinUAE-DBG/drawing.cpp`.
- Engine: `engine/include/eng/graphics/blit_job.hpp` (destino/tamaño de job), `engine/include/eng/graphics/bob.hpp`/`bob_ring.hpp` (BOBs), `engine/include/eng/graphics/frame_plan.hpp` (lotes), `engine/include/eng/field/tile_animation.hpp` (jobs del parche); demo 207 (fogata/flores/agua) y 208 (objetos sobre fondo).
