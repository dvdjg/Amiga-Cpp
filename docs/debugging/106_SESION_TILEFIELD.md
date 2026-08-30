# Sesión de desarrollo: API TileField + demo 106 (scroll infinito doble página)

Fecha: 2026-08. Objetivo: evolución de la demo 102 hacia una API reutilizable de
campos de tiles con scroll infinito por doble página, blits (nunca CPU), índices
u16 con `Span`, y compositor DPF. Esta nota recoge lo aprendido para que no se
pierda entre hilos.

## Estado final

- `engine/include/eng/field/tile_field.hpp`: `TileLayerMap` (Span), `TileFieldConfig`,
  `TileFieldState`, `TilePendingStrip` (región 2D + cursor), `FieldHardwareView`,
  `BitmapFieldConfig`, `TileFieldController`.
- `engine/include/eng/field/dpf_composer.hpp`: `DpfDisplayComposer`.
- `demos/106_tile_field_showcase/`: demo ÚNICA parametrizable (dual 3+3 o
  single 5 planos, tiles 16/32/48px). Sustituye a 106 y 107.
  ambos ejes, Lissajous de 2 pantallas, seno de alta resolución.
- Diseño completo: `docs/architecture/TILE_FIELD_API.md`.

## Fórmula canónica del scroll (ACE/HRM) — CRÍTICO

La fórmula "naive" (`BPLCON1 = fine`, puntero `= coarse`, `DDFSTRT=$38`) produce
un salto de ~31px en el cruce de tile de 16px. La fórmula probada en 101-104 y
usada en la 106:

```
DDFSTRT = $30   (fetch de 42 bytes = 40 visibles + 2 de margen)
fine     = (16 - (scroll & 15)) & 15
fetch_px = (scroll - 1) & ~15      // clamp a 0 en scroll==0
puntero  = fb + página_activa*40 + fetch_px/8 + y*row_bytes
bpl1mod  = row_bytes - 42          // 640/8 - 42 = 38
```

Así `display_start == scroll` continuo en todo el rango.

## Bug de "saltos de color" en la 106 — RESUELTO (era diseño, no bug)

Síntoma: el assert `assert-no-inner-black` detectaba ~2500-4000 píxeles negros
dentro del viewport en los frames tardíos de una secuencia, y el usuario veía
"saltos de colores" ocasionales.

### Causa raíz (confirmada): el tramado 50% del primer plano

Los píxeles negros se distribuyen **exactamente 50/50 entre filas pares e
impares** en la franja inferior. Ese es el patrón checker `0xaaaa/0x5555` del
fondo del primer plano (PF1), que es **tramado al 50% transparente**: la mitad
de los píxeles del tile del fg quedan en color 0 (transparente en DPF) y, donde
el fondo PF2 detrás también tiene color 0, el resultado visual es negro puro.

Es **contenido intencional del diseño** (la demo 102 usa el mismo tramado pero
su fondo es opaco en las zonas donde el fg es transparente; la 106 deja ver
píxeles negros del borde de color 0). El modelo de visión local confirmó sobre
el frame completo: "perfect, no holes, overall: perfect".

### Evidencia de que NO era un bug de redibujo

- Subir `max_tiles_per_frame` de 16 a 24 a 32 (y el budget del plan de 8 a 80
  jobs) **no cambió el ratio de negro** (~0.012 siempre): un redibujado lento se
  completaría y el agujero desaparecería.
- Los componentes negros son 753 piezas de 2-56px dispersas, **ninguna de tamaño
  tile** (16px reales = 32px de captura a escala 2x): no son tiles sin dibujar.
- El par/impar es exactamente 0.50, el sello del tramado checker.

### Fix aplicado

`analyze-sequence.sh` usa umbral `0.02` (2%) para el assert, con comentario que
explica el tramado del fg. El presupuesto se deja en `max_tiles_per_frame = 32`
(margen seguro para el cruce diagonal) aunque no era la causa del "negro".

### NOTA para futuros hilos

El "salto de colores" ocasional que ve el usuario puede ser el cruce de página
(el display salta de una página a otra preparada). Verificar con secuencia larga
y visión si hay un parpadeo puntual; el análisis estático de frames no lo capta
si es de 1 frame.

## Unificación de demos y test unitario del algoritmo (HECHO)

Para no tener que adaptar cada demo con cada feature, se consolidó en UNA demo
parametrizable y se añadió un test unitario del algoritmo.

### Demo única parametrizable (`demos/106_tile_field_showcase`)

Sustituye a las antiguas 106 (dual scroll infinito) y 107 (tiles anchos). Se
configura por macros de compilación:

- `K_TILE_WIDTH` = 16/32/48: anchura de tile (múltiplo de 16), una pasada del
  Blitter por tile ancho.
- `K_DUAL` = 1/0: dual 3+3 o single 5 planos (sin DPF).

La abstracción de playfield (`TileFieldController` + `DpfDisplayComposer`) es la
misma en todos los casos: un controlador por playfield, sea dual o single. Un
videojuego con un solo PF de 5 bitplanes usa exactamente el mismo código.

`engine/include/eng/field/tile_demo.hpp` centraliza las utilidades comunes de
las demos (paleta, glifos, seno Q16, cámaras, `build_tile_cache` con tiles
anchos) para no duplicarlas.

### Test unitario del algoritmo (`tools/analyze/verify-tile-field-fill.mjs`)

Replica la lógica EXACTA de `begin` (offset absoluto) y `update` (delta relativo)
y verifica invariantes sobre el framebuffer de doble página:

- `begin(offset)` en 7 zonas distintas: cobertura completa, sin solapes, y cada
  celda física contiene el tile correcto del mapa (world = page_origin + offset).
- `update(+N)` sin cruzar página: no encola franjas nuevas.
- `update(+320)` cruzando página: encola la página vacante, cobertura total OK.
- Inversión (cambio de signo del delta): reencola la página opuesta con el tramo
  trasero y la página que queda atrás con el tramo anterior.
- `update(+1px)` repetido: nunca encola (el cruce es de página, no de tile).

El `analyze-sequence.sh` del showcase ejecuta el test antes de la captura.

### Build parametrizable

`tools/build/build-demo.sh` acepta `EXTRA_DEFINES` (p. ej.
`EXTRA_DEFINES="-DK_TILE_WIDTH=32"`) para compilar la showcase en cualquier
configuración sin editar el fuente.

## BUG del "petardazo" al invertir la cámara (RESUELTO: scroll bidireccional)

Síntoma del usuario: la 106 comienza con scroll suave pero "llegado a un punto
petardea y se enlentece, modificándose plaquetas en áreas visibles".

### Causa raíz (múltiples bugs del scroll bidireccional)

El fg hace una Lissajous que va y viene (0..640 X, 0..512 Y). Con el diseño de
doble página que rellenaba la página opuesta SIEMPRE con el tramo delantero, al
INVERTIR la cámara la página que se iba a mostrar había sido sobrescrita con el
tramo de avance -> contenido incorrecto visible (plaquetas). Además:

1. `enqueue_strip` no reutilizaba slots inactivos -> tras el estampado inicial
   (4 slots) las franjas del scroll NUNCA se encolaban (bug ya resuelto en otro
   commit).
2. Al invertir, la página opuesta se reencolaba con el MISMO slot físico pero
   DISTINTO tramo: dos franjas activas escribían el mismo slot con contenido
   contradictorio ("petardazo"). Ahora `enqueue_strip` CANCELA la franja activa
   del mismo slot físico si el tramo cambia.
3. `enqueue_page_x` redibujaba la banda X con un SOLO `world_y` (el de la página
   Y activa), sobrescribiendo la página Y opuesta con tiles incorrectos. Ahora
   encola DOS franjas por cruce X (una por página Y, cada una con su world_y).
4. El presupuesto de 32 tiles/frame por campo no vaciaba la cola en el cruce
   diagonal (1280 tiles encolados): subido a 56/frame (2 campos = 112 jobs,
   dentro del max_blit_jobs=128 del FramePlan) y budget del plan a 120 jobs.

### Estado final verificado

- `max_pending` (16 bits, bits 20-31 del marker) llega a ~1224 en el cruce
  diagonal (lo esperado: página X 640 + página Y 640) y la cola se VACÍA a
  0-1 tiles entre cruces (los frames posteriores muestran pending bajo).
- Visión local: scroll continuo, sin tile-pop, sin artefactos, confianza 1.0,
  en secuencias que cubren el cruce de la Lissajous en ambos sentidos.
- El cambio de `max_blit_jobs` de 128 a 256 ROMPÍA el init (fallo 0x10604):
  el FramePlan vive en BSS y al crecer colisionaba; se mantuvo en 128.

## BUG REAL del "salto de color" en el cruce (RESUELTO: enqueue_strip)

Tras instrumentar la demo con telemetría de "máximo de tiles pendientes" se
descubrió la causa real del salto de color ocasional que ve el usuario:

**`TileFieldController::enqueue_strip` nunca reutilizaba slots inactivos.** El
estampado inicial (`enqueue_initial_strips`) encola 4 franjas (2x2 páginas del
framebuffer 640x512). Al terminar, `pending_count == 4` aunque todos los strips
estén `active=false`. La función hacía `if (pending_count >= 4) return;`, así que
**las franjas del scroll (página vacante) NUNCA se encolaban**: el fondo/el fg se
quedaban con el tramo de mundo inicial y al cruzar la página el display mostraba
contenido viejo -> "salto de color" de 1 frame.

Síntoma en telemetría: `max_pending` siempre 0 (nunca había trabajo pendiente),
a pesar de que el cruce en diagonal encola 640+640 tiles.

Fix: `enqueue_strip` busca el primer slot `active==false` y lo reutiliza
(`free_slot`); solo si no hay ninguno libre añade un slot nuevo. Tras el fix la
telemetría muestra `max_pending=249` (trabajo de redibujado en marcha, correcto) y
el modelo de visión confirma continuidad perfecta.

Regresión del analyze-sequence: exige `max_pending > 0` (si es 0, el bug de
enqueue_strip ha vuelto) + cruce de página en ambos ejes.

## Trompicón del movimiento (RESUELTO: precisión Q16 + acumulador de resto)

Síntoma: la Lissajous del fg "iba a trompicones", perdía fluidez.

Causa raíz: `sin_smooth` devolvía un entero y `fg_wave_camera` hacia
`sin * 320 / 64` = `sin * 5`. El seno redondeado cambiaba en pasos grandes
(0,0,5,0,5 px en X), porque la cuantización a entero amplificaba la velocidad por
5 en X y 4 en Y.

Fix:
- `sin_smooth` devuelve ahora el seno con precisión **Q16** (escala 65536 = 1.0):
  `(s0*(64-frac)+s1*frac) * 1024`, conservando la fracción interpolada.
- `fg_wave_camera` devuelve la posición en px*65536 (sub-píxel).
- `update` calcula el delta en Q16 y extrae la parte entera en px con un
  **acumulador de resto** (`fg_rest_x/y`): la fracción nunca se pierde, el
  movimiento es 0.7-3.8px/frame continuo en vez de saltos de 5px.
- Periodos acortados a 640/480 frames: velocidad pico ~3.8px/frame (dentro de
  `max_delta=8`) y cruza una pantalla (320px) en ~160 frames (~3s), visible.

## Transparencia total del tile 63 del fg (pedido del usuario)

`build_tile_cache` ahora escribe el tile 63 del PRIMER PLANO con todas las filas
a 0 en todos los planos (totalmente transparente). `build_map` ya ponía el tile
63 en ~50.7% de las celdas del fg (`(h>>9)&1`). En DPF, donde el fg es 0
(transparente) y el fondo PF2 tiene su color 0 (registro 8, transparente), el
resultado es negro puro: es el diseño pedido, no un defecto.

Consecuencia: el assert `assert-no-inner-black` ve más negro puro (~3%). El
`analyze-sequence.sh` de la 106 usa umbral 0.05 con la justificación en el
comentario.

## Tiles anchos (múltiplos de 16px) — VALIDADO

`TileFieldConfig::tile_width` admite 16/32/48/64px. `make_tile_copy_job` genera
un ÚNICO job `TileBlockCopy` con `words_per_row = tile_width/16` (2 para 32px,
3 para 48px, 4 para 64px), cubriendo el tile ancho en una pasada del Blitter.

Validación:
- **Test host** `tools/analyze/verify-tile-width-jobs.mjs`: replica la geometría
  exacta del job (words_per_row, modulo, stride de fuente/destino) y la
  coherencia del layout del tileset ([tile][plano][filas x words_per_row]
  contiguos, sin solape entre planos ni tiles). Pasa para 16/32/48/64.
- **Demo runtime** (showcase `demos/106_tile_field_showcase` con
  `EXTRA_DEFINES="-DK_TILE_WIDTH=32"`): el modelo de visión confirma scroll
  continuo sin artefactos y el análisis de píxeles confirma que las dos mitades
  de cada tile de 32px difieren (ambos words se copian).

Nota: el `build_tile_cache` del header común `eng/field/tile_demo.hpp` escribe el
layout contiguo con palabras de variante por mitad para verificar visualmente que
el tile ancho se dibuja completo.

## Migración de la demo 102 a la nueva API (HECHO)

La 102 (`demos/102_tile_scroll_dualpf`) se migró de `TileScrollScene<Mode>` a la
nueva API: dos `TileFieldController` + `DpfDisplayComposer`.

- Se mantuvo la identidad: modo dual 3+3, paleta, tiles de glifos hex con tile 63
  transparente, mundo finito 64x32 (sin wrap), parallax por fases (`RouteCamera`)
  + Lissajous.
- El movimiento del fg usa precisión Q16 (seno interpolado + acumulador de resto)
  como en la 106.
- **Lección**: al migrar, no usar el Lissajous GRANDE de la 106 (0..640) en un
  mundo FINITO: la cámara se clava en el borde (max scroll Y del mundo 64x32 =
  256) y aparece un salto visible. Restaurar el rango contenido (64..256 X,
  32..224 Y) que respeta el mundo.
- `begin` recibe la posición inicial de la cámara (no (0,0)) para que el primer
  update no tenga un salto.
- El analyze-sequence de la 102 valida parallax: posiciones X distintas +
  movimiento en ambos ejes del fg y al menos uno del bg.

## Lecciones de herramientas (Windows + Git Bash + WinUAE-DBG)

- El `bash` del PATH (`C:\Windows\System32\bash.exe`) es WSL y **manga las rutas
  Windows**. Usar SIEMPRE Git Bash real: `C:\Program Files\Git\bin\bash.exe`.
- build-demo.sh necesita el toolchain: pasar por delante
  `AMIGA_BIN_PATH="C:/Users/dvdjg/.vscode/extensions/bartmanabyss.amiga-debug-1.8.1/bin/win32"`.
- Los scripts del repo (`tools/build/build-demo.sh`, `tools/run/run-demo.sh`,
  `tools/analyze/*`) se invocan con Git Bash.
- Captura de secuencia:
  `bash ./tools/run/run-demo.sh demos/106_tile_field_showcase --warp --sequence-frames 8 --sequence-interval-ms 800`.
- `--warp` está en la config uae pero `MinimalBackend::boot()` llama
  `set_warpmode(false)`, así que el run avanza en tiempo real (~50fps). Para que
  la cámara cruce páginas en la ventana de captura hay que alargar el intervalo
  (800ms) o el número de frames; el `analyze-sequence.sh` de la showcase usa
  8x800ms.

## Lección sobre el análisis de imágenes (PNG)

- Para analizar screenshots/frames usar SIEMPRE
  `import { readPng } from './dist/tools/lib/image.js'` (decodifica bien, RGBA).
  Un parser PNG manual con `zlib.inflateSync` **da datos corruptos** si no aplica
  los filtros de scanline de PNG -> análisis erróneos de "negro".
- El assert `tools/analyze/assert-no-inner-black.sh` calcula el bbox de TODOS los
  píxeles no negros de la captura (756x576). El overlay de debug de WinUAE
  extiende ese bbox fuera de la ventana real del juego, así que puede reportar
  "negro interno" que en realidad es el borde del emulador. **Validar siempre
  con la ventana real centrada 320x256**: en la 106 el peor frame tiene 0.36% de
  negro dentro de la ventana (correcto), mientras el assert general daba ~1%.
- La demo 102 pasa el assert con 0 negros; la 106 en frames 6-7 daba ~2500 negros
  por el mismo motivo del bbox (el bug real de presupuesto, ya resuelto).

## Modelo de visión local (Ollama) — cómo usarlo

- Servidor local: `http://127.0.0.1:11434` (API de Ollama).
- Modelos disponibles: `qwen3:8b`, `qwen2.5-coder:7b`, `qwen3-vl:8b-instruct-q8_0`
  (visión), `gemma3:12b` (visión).
- Flujo recomendado (vision-review del repo):
  1. `node tools/vision-review/vision-review.ts --root . --source <dir> --profile amiga-scroll-transition --outDir <out>` (genera request.json);
  2. `node tools/vision-review/vision-review.ts --reviewRequest <out>/request.json --provider tools/vision-review/providers/ollama.local.json`.
  `ollama.local.json` usa `qwen3-vl:8b-instruct-q8_0`, multi-image, JSON estricto.
- El perfil `amiga-scroll-transition` comprueba continuidad del scroll, tile-pop,
  cambios de paleta, artefactos. Resultado de la 106: scroll continuo, sin
  tile-pop, sin artefactos, confianza 1.0.
- Endpoint para llamadas directas con visión: `/v1/chat/completions` con
  `content` como array de `{type:'text'}` + `{type:'image_url', image_url:{url:'data:image/png;base64,...'}}`.
  El endpoint `/api/chat` de Ollama NO acepta ese formato de imagen (400).

## Decisiones de la API (resumen)

- `Span` en lugar de punteros crudos (`const u16* cells`): `at()` con trap ante
  violación de rango, sin aritmética de punteros en la API.
- Dibujo SIEMPRE por Blitter (`TileBlockCopy` vía `FramePlan`): nunca CPU. El
  estampado inicial usa `begin` (encola) + bucle de `pump` en init hasta
  `busy()==false`.
- `tile_width` múltiplo de 16 (16/32/48/64px) en UNA pasada del Blitter
  (`words_per_row = tile_width/16`); asignación de tiles anchos individuales es
  fase posterior.
- `max_delta_x/y` (tope de salto por frame) y `max_tiles_per_frame` (presupuesto)
  son dos palancas distintas, fijadas a priori.
- Franjas como regiones 2D con cursor (no columnas simples), para cubrir tanto el
  estampado inicial como la página vacante.

## Comandos canónicos de la demo showcase (106)

```
# Build (dual 3+3, tiles de 16px por defecto)
AMIGA_BIN_PATH="C:/Users/dvdjg/.vscode/extensions/bartmanabyss.amiga-debug-1.8.1/bin/win32" \
  bash ./tools/build/build-demo.sh demos/106_tile_field_showcase --debug --clean

# Build con tiles de 32px o single 5 planos
EXTRA_DEFINES="-DK_TILE_WIDTH=32" bash ./tools/build/build-demo.sh demos/106_tile_field_showcase --debug --clean
EXTRA_DEFINES="-DK_DUAL=0"        bash ./tools/build/build-demo.sh demos/106_tile_field_showcase --debug --clean

# Run + secuencia
bash ./tools/run/run-demo.sh demos/106_tile_field_showcase --warp \
  --sequence-frames 8 --sequence-interval-ms 800

# Regresión de la demo (incluye el test unitario del algoritmo)
bash ./demos/106_tile_field_showcase/analyze-sequence.sh --warp

# Test unitario del algoritmo begin/update (host)
node tools/analyze/verify-tile-field-fill.mjs

# Visión
node tools/vision-review/vision-review.ts --root . \
  --source out/run/106_tile_field_showcase/sequence \
  --profile amiga-scroll-transition --outDir out/vision-review/106_x
node tools/vision-review/vision-review.ts --reviewRequest out/vision-review/106_x/request.json \
  --provider tools/vision-review/providers/ollama.local.json
```
