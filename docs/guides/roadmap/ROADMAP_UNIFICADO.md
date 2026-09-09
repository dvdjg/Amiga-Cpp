# Roadmap unificado del engine Amiga (reunificación)

Fecha: 2026-09. Este fichero **reúne** los roadmaps/disposiciones dispersos y fija
el estado real del engine y de las demos, para decidir por dónde seguir.

## Inventario de roadmaps / documentos de dirección

| Documento | Estado | Qué cubre |
|---|---|---|
| `docs/guides/roadmap/XLIMITED_8WAY_EHB_201.md` | roadmap original 201→engine→202 | F1-F4 del corkscrew 8-way EHB de 201; **F5 = «Demo 202 DPF (+ features del 107)»** (DPF 3+3 + transparencia, corkscrew, variantes, telemetría) |
| `docs/guides/roadmap/REGLAS_PIPELINE_TILES.md` | reglas vigentes | Pipeline tiles/EHB (reglas de oro, no romper el 100 %) |
| `docs/demos/tile-pipeline/PIPELINE_TILES_EHB.md` | guía completa | Cuantizar → slice → emit → verificar (round-trip 100 %) |
| `docs/guides/roadmap/PROBLEMA_LAUNCHER_DEMOS_NUEVAS.md` | RESUELTO | Demos nuevas que quedaban en AmigaDOS |
| `docs/guides/roadmap/TILED.md` | conocimiento preservado | Formato Tiled (.tmx/.tsx), a usar cuando haya metadatos |
| `docs/engine/architecture/DPF_MIXTO_SPLIT_LINEAL.md` | (nuevo) | DPF: modos de Y por campo (split/lineal/mixto) |

## Estado real del engine y las demos (2026-09)

- **Scroll**: corkscrew 8-way X-Limited (`XLimitedPlayfield` + `ScrollEngine` +
  `ScrollSink`), tiles interleaved de 320 px, wrap toroidal, anillo vertical.
- **DPF 3+3** (`XlimitedDualComposer` + `XlimitedScene`): dos playfields con
  banco/mapa/paso propios, transparencia del FG (PF1 color 0), `BPLCON1` por
  campo; bancos reales por campo a 3..6 planos (`blocks_prebuilt`, `blocks_prebuilt2`).
- **Y por campo (flexible, paramétrico, sin macros)**: `dual_linear_field` +
  `linear_display` + split por campo en el compositor. Modos: corkscrew dual
  (Y compartida), lineal dual (Y independiente en ambos, 2× mirror) y mixto.
- **Demos**: 107 (corkscrew/DPF showcase), 201 (EHB mapa real, tour 8-way), 202
  (DPF 3+3 mapa real 8c + plaquettes, recorrido con Lissajous, por defecto FG
  lineal/Y propia + BG corkscrew/split, variante `kShareY`).
- **Telemetría por frame** leíble (símbolo en `.data`), lectura arreglada.
- Sin `linear_display` por defecto en 202: viewport recortado 320×208 para split
  canónico; mapas toroidales.

## Sprites hardware — estado (2026-09)

- **Hecho**: `SpriteTemplate` + `SpriteManager::emit_template_into` (multiplexado
  vertical "chasing the raster" + color multiplexing) validados por la demo 053.
  Se corrigió la codificación de `SPRxPOS`/`SPRxCTL` (VSTART byte alto, HSTART÷2 en
  byte bajo; ver `amiga-bootcamp/08_graphics/sprites.md`) y los offsets de registro
  (antes caían en registros de audio).
- **Pendiente (mejoras apuntadas)**:
  - `SpriteAllocator` (paso 4 de `ENGINE_DESIGN.md` §5): asignar canales a
    `SpriteIntent` con multiplexado y decidir el overflow → BOB (transición
    sprite→BOB transparente). Es el siguiente paso.
  - Diagnosticar por qué **rellenar bitplanes rompe el rearm** del sprite en modo
    6 planos (solo dibuja el primer segmento); bloquea fondos reales en demos de
    sprites. Alternativa segura: fondo por copper-gradient (`COLOR00` por línea).
  - Materializar los `CopperIntent` que faltan (`ShiftLines`, `BitplaneSplit`,
    `SpriteRearm`, `Priority`) en el driver que conoce el layout (paso 2/3).
  - Embellecer la demo 053 (fondo, animación de colores) siguiendo la regla
    «Demos atractivas» de `AGENTS.md`.

## Decisiones tomadas en 202 (a respetar)

1. El scroll debe ser **un único algoritmo toroidal** (sin modos de borde finito);
   el recorrido se acota a un primer paso del mundo para no mostrar la costura.
2. Sin `linear_display` global salvo elección explícita; para Y independiente se
   usa el modo por campo (split + lineal/mirror).
3. Sin macros de selección en C++: constantes paramétricas (`if constexpr`) o
   plantillas; el engine configura por datos.
4. Telemetría y `analyze-sequence.sh` como gate por demo (movimiento continuo +
   regresión de la Y independiente).
5. Pipeline de tiles por defecto: paleta `mediancut` + dither `atkinson` a 8
   colores. Se añadió el dither `checker` (checkerboard de dos colores cercanos,
   estilo core-design, para sintetizar colores perdidos tras la cuantización) y
   los nombres de tiling incluyen tamaño de tile y dither (`…c_tN_<paleta>_<dither>…`).

## F6 — Variante rápida «Sonic» (medición E1, 2026-09)

Objetivo: pasos >2 px/frame (5-6 px, estilo Sonic) garantizando que la fila/
columna entrante se dibuja siempre a tiempo, y tiles de 32×32.

Medición E1 (parámetro `kFastStep` en la 202, sin macros): con `kFastStep=6` la
telemetría real da `blit_jobs` máx 27 (media 12) y `blit_words` máx 1296 → carga
baja para 50 fps; el cruce de 16 px pinta su columna/fila dentro del mismo frame
(paint-then-advance) → **no hace falta draw-ahead por coste** a estas velocidades.

Pendientes de F6:
- Verificar/arreglar el camino completo de tiles **32×32** (banco X-Limited,
  fetch/guardas, fillup) en el engine y validarlo en una demo/variante.
  Estado: el direccionado del banco en `draw_block_job` ya es genérico (320 px →
  `320/tile` bloques/fila, words = tile/16). **Bloqueo de algoritmo**: el 8-way
  X-Limited recorre el tile píxel a píxel con `fetch_scroll_pixels = 16` y
  `BPLCON1` de 4 bits (0..15), es decir está pensado para tiles de 16; con tiles
  de 32 el fine del chipset (máx 16) no cubre el tile completo. Opciones:
    A) Tratar el tile de 32 como dos macro-pasos de 16 (redibujo/plane-shift cada
       16 px dentro del tile) — extensión del algoritmo.
       Diseño (resumen A): el 8-way conserva el fine de 16 (BPLCON1 0..15) y el
       pointer avanza cada 16 world-px; cada tile de 32 se divide en DOS «macro-
       celdas» de 16 que se redibujan por separado según cruce. Concretamente:
       - `draw_block_job` ya escribe words=tile/16 (2 en 32) → dibuja el tile
         completo cuando se pinta su celda.
       - El scroll (scroll_right/left/down/up) pasa a cruzar cada 16 px de la
         macroc el del (en lugar de cada tile): al cruzar el 1er macro-paso de un
         tile 32 se muestra la mitad ya dibujada; al cruzar el 2º se redibuja el
         siguiente tile. Es un cambio acotado a `scroll_engine.hpp` (granularidad
         de cruce = 16 fijo, no tile) + el fetch/guarda de `tile_width`.
    B) En el test DPF BG=32/FG=16, usar scroll 16 para el BG (tiles de 32 solo
       como «macro-celda» de contenido, movimiento 8-way cada 16) — sin cambiar
       el fine del algoritmo.
       Nota tras análisis: con fine 16 y celdas de 32 «de contenido», el test se
       reduce a la demo actual de 16; no ejercita de verdad tiles de 32. Solo la
       opción A valida 32 real en 8-way.
    C) Posponer 32 hasta documentar bien A.
- Decidir si a saltos ≥ 2 cruces/frame conviene pre-dibujar N columnas/filas según
  la velocidad (opción paramétrica del scroll, sin tocar la regla de dibujo).

## Preguntas abiertas y próximas direcciones (a decidir)

Opciones (no excluyentes, conviene priorizar):

- **A. Capa de bobs/personajes en el FG lineal** (estilo Megatyphoon: escribir
  solo donde el bob no solapa a otros, sin releer pantalla). Aporta el “juego de
  plataformas” real sobre la base DPF.
- **B. Varias capas de parallax** (BG mero fondo + FG plataformas 8-way, estilo
  Jim Powers): demostrar/validar con 2+ capas y Y por capa.
- **C. Mapa/editor y metadatos**: retomar Tiled (metadatos, objetos, spawns) y el
  camino completo de pipeline hacia un nivel jugable.
- **D. Escenas/juego end-to-end**: elegir un caso de uso (plataformas o shooter 8
  way) y construir la primera escena «de juego» encima del engine parametrizado,
  cerrando el F5 de forma real (harness por feature).
- **E. Robustez/rendimiento**: telemetría con gate estricto 50 fps en las demos y
  presupuesto de Chip RAM por modo DPF (matriz de memoria documentada).

Recomendación inicial: **D** (caso de uso concreto, p. ej. plataformas con BG
fondo + FG mapa 8-way, reutilizando 202/DPF) para cerrar el F5 y forzar el resto
de decisiones (bobs, Tiled, memoria) con un objetivo real; en paralelo **E**
ligero (gate 50 fps + tabla de memoria) para no perder el presupuesto.
