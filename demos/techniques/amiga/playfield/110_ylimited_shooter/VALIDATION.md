# VALIDATION — Demo 110 (`ylimited_shooter`)

Informe de validación (F4/F5 de `docs/guides/methodology/PROCEDIMIENTO_DEMOS_Y_JUEGOS.md`).
**Estado: NO VERIFICADA** — deuda abierta en `docs/guides/roadmap/ROADMAP_DEUDA_TECNICA.md` **DT-001**.

Intención esperada (según `src/main.cpp`): fondo de tiles (tileset generado de 16 glifos × 8
variantes) con scroll Y (corkscrew) y X finito; capa FG de objetos: nave abajo con vaivén
horizontal, balas blancas subiendo y torreta verde fija arriba-izquierda que apunta a la nave.

## Mediciones

| Medida | Comando | Resultado | Lectura |
|---|---|---|---|
| fps debug | `node tools/debug/measure-fps.mjs 110_ylimited_shooter A500_debug --json` | **11,53 fps · 4,34 fields · 615.160 ciclos/frame** | 4,3× el presupuesto (~141,9k) |
| fps release | idem `A500_release` | **14,63 fps · 3,42 fields · 484.990 ciclos** | sigue 3,4× fuera de 50 fps |
| elementos (40 frames) | `node tools/analyze/check-elements.mjs out/run/110_ylimited_shooter/A500_debug/sequence "#00ff00,#ffffff,#ff4400,#ffdd00,#77eeff"` | verde (torreta) 980–1024 en 40/40; rojo (nave) 240–256; blanco (balas) 72–304; amarillo (cañón) 0–44 | torreta y nave **siempre presentes** (primeros auxilios OK); balas variables por cadencia |
| bandas | ídem con `frame_000.png frame_020.png` | **y=448..575 del PNG (2×) diff 0.00 → 64 líneas Amiga inferiores ESTÁTICAS** | bug de scroll (DT-001 F2) |
| traslación por pares consecutivos | `shift-pairs` (franja de tiles, 40 pares) | offsets +4/+8 px PNG (2–4 px Amiga) con `mad≈0`, salvo pares con `mad≈36` (balas cruzando la franja) | el scroll Y **sí traduce** en la zona alta; la franja baja no |
| movimiento nave/torreta | `motion-audit` (bbox por color) | nave `x=368→444→410` (vaivén suave ±1 px Amiga/frame); torreta `x=124..155` fija | animación real, contraria a lo que dijo la visión |
| **ablación F1 (sin FG)** | compilar con el bloque FG desactivado + `measure-fps` | **39,9 fps · 1,25 fields · 177.805 ciclos** | **el FG cuesta ~307k ciclos/frame (63%)**; el resto (scroll+compose+bucle) 178k (25% sobre presupuesto) |
| captura determinista | `run-demo.sh … --sequence-step-frames 16` | `frame_NNN_fNNNN.png` consecutivos (f0008…f0023) | DT-005 resuelto: usar este modo para validar |

## Presupuesto por elemento (estimación inicial, a confirmar en F1)

Desglose mental del bucle (`update`+`render`), con las rutas que no se ejecutan siempre marcadas:

| Elemento del bucle | Coste esperado | Notas / peor caso |
|---|---|---|
| `update_scroll` + `execute_frame_plan` (blitter de tiles) | **candidato principal**; crece con el salto de cámara | `max_step=4`; el coste real está sin medir (F1) |
| `compose` + `install` (parcheo de copperlist) | fijo por frame | documentado como camino caliente parcheado (~2–3k ciclos) |
| FG (CPU): nave 4 fills + balas ≤6 pares + torreta 1 cuerpo + ≤3 puntos + ≤3 borrados | medido indirectamente: el hueco borrado→repintado de la torreta se cruzaba con el barrido a ~1 ms a -O0 | primer auxilio: sin borrado del cuerpo y borrado de cañón contiguo |
| Telemetría (`detail`) | despreciable | — |
| **Rutas no frecuentes** | | |
| `update_scroll` devuelve `false` (tope del mundo) → `set_camera` a abajo | **pico**: repintado completo del mapa | ocurre al llegar arriba (peor caso periódico; candidato a tirón) |
| Respawn de bala (cada 8 updates) | bajo | — |
| `execute_frame_plan` con presupuesto excedido → falla | camino de error | `plan.set_blit_budget_limits(...)` |
| `init`: `fill`+`compose`+self-tests (`util_selftest`, `fixed_math_selftest`) | alto pero **fuera del bucle** | no debe contar para el frame |

El **peor caso medido** es el frame completo: 485k ciclos release (3,4× presupuesto); F1 del
roadmap debe atribuir esa cifra a los elementos de la tabla con el perfilador.

## Prompts y respuestas (crudos)

### A · Inventario sin contexto (3 frames: 0,20,39)

**Prompt** (idéntico al de `DEMO_VISUAL_DEBUG.md` §6.4, con N=3, sin adaptar):
> Vas a analizar 3 capturas consecutivas (frames 0..2) de la misma animacion. No conoces el programa ni lo que pretende mostrar; describe SOLO lo que observas. 1) Inventario: enumera los elementos distintos que ves (forma, color, tamano relativo) y cuantos hay de cada tipo. 2) Dinamica: para cada elemento, cambia de posicion, tamano, forma o color entre frames? como se relaciona con los demas (orden, solapes, capas)? 3) Algun elemento aparece o desaparece en algun frame? alguno parpadea? 4) Ves discontinuidades, cortes, zonas incoherentes o basura (ruido, bloques, texto raro)? Indica zona relativa (arriba/centro/abajo, izquierda/centro/derecha) y en que frames. 5) El conjunto se mueve de forma coherente? hay algo que deberia moverse y no se mueve? Responde en espanol con observaciones concretas. No inventes coordenadas en pixeles.

**Respuesta** (cruda, extracto literal):
> Se observan 3 elementos principales […]: cuadrado verde brillante arriba-izquierda; icono en forma de cono/vela azul claro con base naranja abajo-derecha; punto blanco muy pequeño en el centro. […] El cuadrado verde permanece en la misma posición en todos los frames. El icono azul-naranja también permanece en la misma posición […]. El punto blanco se mueve […]. No hay ningún elemento que aparezca o desaparezca […]. No se observan discontinuidades, cortes, zonas incoherentes ni ruido […]. El conjunto […] el fondo permanece fijo […] y el punto blanco es el único que cambia de posición.

**Contraste objetivo:** la nave **sí se mueve** (bbox rojo: 368→428 px PNG en 20 frames) y el fondo **sí se desplaza** (traslación limpia +4/+8 px por captura). Falsos negativos.

### B · Dinámica/seguimiento (frames 0,13,26,39)

**Prompt:** céntrate en el icono azul claro con base naranja (nave); sigue su evolución frame a frame (posición, tamaño, aspecto, saltos/paradas); ¿el fondo se desplaza verticalmente o permanece fijo?

**Respuesta** (cruda, extracto literal):
> Analicemos el comportamiento del icono en los dos frames proporcionados: […] En ambos frames, el icono se encuentra en la misma posición relativa […]. El fondo […] permanece fijo en ambos frames. […] Conclusión: el icono permanece fijo en su posición durante este intervalo de tiempo. […] El fondo también permanece fijo.

**Contraste objetivo:** falsos negativos (nave y fondo se mueven). Además el modelo declara **«los dos frames proporcionados»** cuando se le enviaron **4 imágenes**.

### C · Verificación dirigida (frames 0,39)

**Prompt:** (intención + lista de elementos esperados + presente/ausente/a medias por frame + continuidad + parpadeos + zona relativa).

**Respuesta** (cruda, extracto literal):
> Dado que solo se proporciona un único frame, es imposible afirmar con certeza sobre el movimiento […]. El bloque verde está estancado y no muestra ninguna forma de punta o brazo. […] Fondo: ausente (no hay desplazamiento visible). Nave: presente. Balas: ausentes. Torreta: presente, pero a medias.

**Contraste objetivo:** con **2 imágenes enviadas el modelo vio 1**; balas presentes en el frame 0 (blanco=184 px); cañón amarillo presente (44 px en la captura de referencia).

### C′ · Verificación con hoja de contacto etiquetada (1 imagen, 4 paneles)

**Prompt:** «La imagen es UNA hoja de contacto con 4 paneles etiquetados (FRAME_000.PNG…) de la misma animacion, en ese orden. Compara los paneles entre si […]: inventario, dinámica, apariciones/parpadeos, discontinuidades y si el fondo se desplaza.»

**Respuesta** (cruda, extracto literal):
> Los caracteres […] cambian de posición entre paneles […] sin un patrón de desplazamiento uniforme […]. No hay aparición ni desaparición; no hay parpadeo […]. No se observan discontinuidades […]. No se observa desplazamiento del fondo entre paneles.

**Lectura:** el montaje etiquetado **sí se atiende** (comparó paneles leyendo glifos distintos), pero la reducción de resolución de la hoja hace que el desplazamiento fino del fondo no se perciba; lo detecta como «cambios aleatorios» de glifos.

## F2 — aislamiento del scroll y captura de ventana (actualizado)

- **Banco aislado** (sin FG, uncommitted): mapa con la fila como glifo (`g_map = y & 15`) y
  captura determinista por paso. El desplazamiento vertical del color compuesto es uniforme
  (−2 px/juego, `mad=0.00` en 11 pares) — **alcance probado**: traslación global de una franja;
  **no** cubre sincronía entre planos, eje X ni la ventana.
- **Captura de ventana** (MCP arreglado, `PrintWindow`): la ventana real muestra **dígitos
  duplicados por filas** (1,1,2,2,3,3…) y **banda basura** inferior → el área visible no se pinta
  bien. El screenshot interno (288 líneas) recortaba esa zona en negro y ocultaba el fallo.
- **Registros en ejecución**: `DIWSTRT=$2981/DIWSTOP=$F9C1`, `BPL1MOD=$0078`, `BPL2MOD=$004E`,
  PF1 a **54 B/plano** vs PF2 a **40 B/plano** con DDF compartido → geometrías de fila distintas.
- Diagnóstico completo y plan en [`docs/reference/amiga/techniques/ylimited-corkscrew.md`](../../../../../../docs/reference/amiga/techniques/ylimited-corkscrew.md)
  (DT-006): la referencia exige bitmap `(SCREENWIDTH+16) × (256+32)`, una sola `row_bytes` por
  plano para PF1/PF2 y el modulo-trick con sobre-fetch; el engine usa `viewport 208 + 288`,
  sin fillup ni sobre-fetch.

## F3 — corkscrew corregido y 50 fps (banco scroll-only, actualizado)

**Causas raíz encontradas y corregidas** (todas verificadas con la copperlist viva por el canal lateral y `qOffsets` por RSP):

1. **WAIT del split obsoleto**: el camino caliente del composer dual (`patch`) parcheaba los punteros cada frame pero **no la línea del WAIT** (no tenía handle). Medido: WAIT en raster 43 (fila 2) con punteros a 142 filas → costura rota. Corregido grabando el handle en `emit_full` y reescribiendo la palabra del WAIT en `patch` (`xlimited_composer.hpp`). Verificado de nuevo: WAIT raster 47 (fila 6) con Δ=234×162 B → 234+6=240 ✓ coherente.
2. **BPLCON1 (fine X) obsoleto**: tampoco se parcheaba en el camino caliente. Corregido con handle del MOVE.
3. **Pico por eje Y con `x=Finite`+`y=Ring`**: un único flag `finite_x()` hacía que el Y usara el atajo «pinta la fila entera al cruzar» → pico de CPU+Blitter (~87k ciclos de blits) cada cruce; medido por secciones: sec0 avg 38k con mínimos de 1k (ráfagas) → 1.252 campos/frame. Corregido con `finite_y()` por eje (`scroll_engine.hpp`): el Y con anillo usa el walk incremental.
4. **Geometría del lienzo PF2**: fila 40 B < fetch 42 B del DDF $30 (desbordaba a la fila del plano siguiente) y sin guarda izquierda. Corregido: `CanvasPlayfield` acepta `row_bytes`/`x_offset_px` (guardas 16/32 px) y la escena crea el lienzo con fila 42 B + offset 16. En vivo: `BPL2MOD=$0054` (84 = 42·3−42) ✓.
5. **Anillo a 240** (= viewport 208 + 2 bloques, como la referencia) en vez del 288 forzado.

**Medidas** (bench aislado de scroll; mapa de glifos por fila; FG desactivado; X patrulla + Y 2 px/frame):

| Estado | fps | ciclos/frame | campos/frame |
|---|---|---|---|
| Antes (con los bugs) | 39.94 | 177 628 | 1.252 |
| Ejes congelados (control del bucle) | 49.92 | 142 102 | 1.002 |
| Con walk genérico del anillo (contenido roto) | 49.92 | 142 102 | 1.002 |
| **Ahora (fila en rodajas, anillo correcto)** | **49.87** | **142 244** | **1.003** |

**Enrollado del anillo (fila en rodajas).** El walk genérico del corkscrew asume anillo también en X; con `x=Finite` escribía la fila entrante con contenido mezclado (visión: f0086 «fila F seguida de 1s»; f0130 «dos bloques 4-8 y A-F»). Corregido en `scroll_engine.hpp`: con `finite_x` + `y=Ring` la fila entrante se pinta **en rodajas** (1/16 de las columnas por sub-paso de 1 px) en la fila fija del anillo (`block_videoposy`), sin walk plane-shifted y sin el pico de fila completa.

**Verificación visual (Ollama, prompts crudos en el informe de ejecución):** misma secuencia y mismos índices antes/después: f0086 (envoltura) marcaba la fila mezclada → ahora f0087 «¿fila que mezcle dos caracteres? No; ¿ruido/bloques? No»; f0130 marcaba los dos bloques → ahora f0131 «sin discontinuidades ni anomalías»; f0089/f0209 limpias.

**Gate de flicker:** `test-regression.sh --flicker --require-flicker-ok` → **Regression OK**, `DuplicatePairs=0`, `ChangedPairs=5`, `MeanDiffAvg=30.3`, sin picos (`MaxDiff=40.1`).

**Artefactos de la pasada** (§6.5 de `DEMO_VISUAL_DEBUG.md`): informe crudo `out/run/110_ylimited_shooter/A500_debug/110_ylimited_shooter_report.md`; capturas con nombre canónico en `out/run/110_ylimited_shooter/A500_debug/vision/110_ylimited_shooter_f0087.png` (y f0089/f0131/f0209); ambas rutas bajo `out/` (gitignored). Herramienta: `tools/analyze/vision-run.mjs`.

**Flicker alterno (banda negra 1 de cada 2 frames) — corregido.** La pasada de 100 frames pedida
(hoja de contacto f0109–f0208, captura desde el frame 100) mostró una **banda horizontal negra en
los frames pares** y limpios los impares: los dos bloques de la copperlist tenían **estructuras
distintas** (uno emitido con split y otro sin él); al cambiar la estructura, `compose()` solo
re-emitía el bloque inactivo y `patch()` reescribía en el otro handles de WAIT/BPLCON1 que no le
correspondían → una de cada dos listas visible corrupta. Corregido en `xlimited_composer.hpp`:
al cambiar la estructura se re-emiten **ambos** bloques. Verificado con la misma pasada (100 frames
limpios, sin bandas ni parpadeo) y gate de flicker **OK**.

**Nota de método (límite del modelo):** el modelo de visión (qwen3-vl) **no detectó** la banda
alterna en la hoja (respondió «sin parpadeo» también en la versión rota); la cazó el agente al
mirar la hoja. Las hojas de contacto (100 frames, celdas etiquetadas) sí permiten la inspección
visual del conjunto; el modelo no sustituye la mirada del agente (§6.4/§6.5 de `DEMO_VISUAL_DEBUG.md`).

**Scroll infinito (toroide).** El mapa se enrolla en Y (`scene_cfg.map.wrap_y = kMapRows`): la fila
-1 es la 127 y la posición de scroll no se reinicia (el motor mantiene `videoposy` módulo del anillo
y envuelve solo la fila de mapa, `scroll_engine.hpp`). **Corregido (tiles corruptos al enrollar):**
la fila del anillo se calculaba con la fila *sin envolver* (`mapy_s = -1` → bloque 14) en vez de la
*envuelta* (127 → bloque 7), pintando los tiles del toroide en el bloque equivocado; ahora
`y_pl = r_dh(wrapped * tile_height)` en ambas ramas (up/down). Comparativa de modelos de visión
(qwen3-vl preferido; gemma3:12b inventa movimientos) — §6.4.1 de `DEMO_VISUAL_DEBUG.md`.

**GLITCH DE ENVOLTURA — ABIERTO (bloqueante #1).** La pasada de 100 frames de la demo completa
(f0011–f0110, mapa real + FG) muestra **f0082–f0088 corruptos** (frames sin mapa, uno negro y una
línea roja vertical) en cada ciclo del anillo; `check-alternating-bands` → FAIL. Causa probable: el
cambio de estructura de la copperlist (presencia del WAIT del split) fuerza re-emitir ambos bloques
(~2×63k) en un frame ya cargado y la instalación/punteros quedan mal varios frames. Un intento de
«emitir el WAIT siempre (aparcado en 0xf8)» empeoró (negro alterno persistente) y se revirtió.
Siguiente paso: volcar la copperlist en los frames del cruce por canal lateral y corregir el camino
de toggle con evidencia.

**FG: rediseño de objetos (torreta con forma y movimiento) e investigación de coste.** La torreta
enemiga ya no es un cuadrado: base, cuerpo, cúpula y ojo (4 rects) + **vaivén** horizontal (24..120
px) además de apuntar al jugador con el cañón (3 puntos). El borrado/repintado se hace en orden
consistente (área previa → forma → cañón), que elimina el detalle «marca amarilla que desaparece»
que marcaban los modelos de visión. Medidas por sección (PROF): FG ≈ 399k ciclos/frame (nave 89k,
balas 188k, torreta ≈122k), con un coste **por llamada** a `fill_rect` de ~12-15k (tanto CPU como
en el fill HW síncrono). Con `set_rect_fill_sink` + `kBlitterRaster` (Auto, ≥64 px) el FG baja de
~8.7 fps (CPU puro) a **12.48 fps**; forzar el fill HW para todo es patológico (501 campos/frame).
Siguiente enfoque (DT-001 F4): **sprites pre-renderizados + blits encolados** (`add_world_bitmap`,
la ruta rápida ya probada por el scroll) en vez de `fill_rect` por objeto.

**Gate anti-flicker alterno en la regresión**: `test-regression.sh --flicker` ejecuta
`tools/analyze/check-alternating-bands.mjs` sobre la secuencia del run (si existe) y marca
`bandas-alternas` como fallo — no depende del modelo de visión (§6.4.1).

**Demo real (FG reactivado, mapa real) — estado actual:** el bloque FG volvió a compilarse tras
muchos turnos y destapó un **address error** introducido al añadir la guarda del lienzo: `byte_for`
devolvía byte impar para `wx=8..15` (se perdió el `& ~1`), y el 68000 no admite escrituras de word
en dirección impar (Guru tras el frame 1; PC en ROM, registros con direcciones impares). Corregido
en `canvas_playfield.hpp` (byte alineado a word, comentado). Verificado: la demo corre con nave,
balas y torreta visibles. Medida actual: **12.28 fps / 577 650 ciclos / 4.072 campos** — el coste
del FG (~307k) domina; su rediseño es DT-001 F4 (pre-render + Blitter/BOB) y es lo que falta para
los 50 fps del demo completo.

**Verificación visual por secuencia** (modo secuencia de `vision-run.mjs`, 6 frames en una llamada
con leyenda de orden temporal): «el objeto azul/naranja de abajo se desplaza… continuo y progresivo
sin saltos, repeticiones ni retrocesos; el objeto verde de arriba permanece estacionario; sin filas
mezcladas, ruido ni zonas corruptas». Respuestas crudas en `110_ylimited_shooter_report.md` (junto a
esta carpeta) y capturas en `vision/` (ambos gitignored).

**Alcance probado / SIN VERIFICAR**: cubre el scroll vertical del anillo (geometría, split,
punteros, mods, enrollado, flicker) y la demo real con FG visible. **Sin verificar**: el fine X con
mapa no uniforme (1 px/píxel) y los 50 fps del demo completo (pendiente del rediseño del FG,
DT-001 F4).

## Conclusiones

1. **Primeros auxilios validados** (commit `3067365e`): nave visible y en vaivén suave, torreta presente en 40/40 capturas (antes: 0/parcial en ~50%), cañón dibujado.
2. **F1 (perfil) resuelto por ablación**: el **pintado del FG en CPU sobre el lienzo Chip single-buffer cuesta ~307k ciclos/frame (63%)**; sin él la demo queda en 1,25 fields (39,9 fps) y el resto (scroll+compose+bucle) son ~178k ciclos (25% sobre el presupuesto). Camino a 50 fps: **rediseñar el FG** (pre-render + Blitter/BOB o sprites) y recortar el resto (DT-001 F1/F4).
3. **La demo sigue NO VERIFICADA**: 3,4–4,3 fields/frame, bandas inferiores estáticas en la ventana en vivo (bug de engine de la familia XLimited: **DT-006**, la 202 las tiene igual; el screenshot interno las recorta en negro), sin fine scroll X (F3), FG single-buffer (F4).
4. **Capacidades observadas del modelo de visión** (a registrar en §6.4):
   - **Multi-imagen: no fiable.** Con 4 imágenes declaró «ambos frames»; con 2 declaró «un único frame». Estrategia operativa: **una imagen por llamada** o **hoja de contacto etiquetada** (que sí compara paneles).
   - **Movimiento lento y patrones periódicos: falsos negativos.** Declaró «nave estática» y «fondo fijo» donde la bbox y la traslación demuestran movimiento; los tiles de glifos periódicos ocultan la traslación.
   - **Aciertos:** inventario de elementos y presencia/ausencia (torreta/nave/balas); en la sesión previa detectó el parpadeo real de la torreta («zonas corrompidas»), que resultó cierto.
   - **Regla operativa:** la visión propone; **la evidencia objetiva decide** (`check-elements`, `band-diff`, bbox/traslación). Un aviso de contenido es bloqueante; un «está bien» no es prueba.
5. **Captura no determinista — RESUELTO**: el modo por intervalo no es estable; para validar se usa `run-demo.sh … --sequence-step-frames N` (paso de frame real, `frame_NNN_fNNNN.png`).

## Deuda registrada

- DT-001 (abierta: F1 hecho por ablación; F2 → DT-006; F3/F4 pendientes) · DT-006 (bandas inferiores XLimited, también en 202) · DT-002 (resuelto: `check-elements --expect`).
- DT-003 ampliado con el hallazgo multi-imagen (protocolo §6.4 actualizado).
