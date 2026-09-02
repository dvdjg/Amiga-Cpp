# Roadmap: Scroll 8-way X-Limited correcto + demo 201 EHB (mapa real) → engine → 202 DPF

Fecha: 2026-09-01 · Decisión: **el 8-way X-Limited es la base; 201 es EHB DESDE EL PRIMER
commit** (aunque la 1ª versión use tiles generados) y el **mapa real es prioritario**.
Tiles 32x32 y pasos de scroll grandes (8..16px) quedan como **secundarios** (apéndice),
no bloquean. Al funcionar se extrae lo reusable al engine y luego se hace el DPF en 202.

## Objetivo
Un motor A500 medible: primero el **scroll 8-way X-Limited impecable**, expuesto en la
demo 201 en **EHB** (32 base + 32 half). EHB va desde el inicio; la 1ª versión muestra los
tiles numerados/de carta (los que ya sirven para ver colocación) y se sustituyen pronto por
el **mapa real de "The Fan-tasy"** (slicing 16x16, sin duplicados, mapa virtual, 320x224).

## Estado actual (punto de partida)
- Harness: canal lateral/READY fiable (`verify-harness.mjs`, sin fallback). Throughput del
  emulador ~11fps; fps-gate informativo hasta que el emulador suba (`--strict-fps`).
- Bug abierto (BLOQUEANTE): tiles de relleno pintados en la zona visible. Diagnóstico:
  watchpoint `g_eng_diag_hit` + breakpoints en `add_draw`(xlimited.hpp:860/877),
  `draw_block_job`(:772), `scroll_down/up`(scroll_engine.hpp:268/318).
- 107 congelada como laboratorio histórico.

## Fases (priorización del usuario)

### F1 · 8-way X-limited sin "tile en área visible" (bloqueante)
Aceptación: `INK==0` en todas las fases, `bands.mjs` sin negro inferior, scroll 8-way 1px
impecable; luego validar 2 y 4px. Nada de lo siguiente depende de 32x32 ni pasos grandes.

### F2 · Demo 201 EHB (desde el primer commit)
- EHB activo en la 1ª build (BPLCON0 EHB + 32 registros base + plano half). El half-bright
  NO cambia el algoritmo de scroll, solo la cuantización: primera versión con los tiles
  generados (numerados/carta) ya cuantizados a la paleta EHB.
- Aceptación: la 201 corre en EHB (32 colores usables + half) con el scroll 8-way de F1 a
  320x224 y el harness verde.

### F3 · Mapa REAL en 201 (prioridad)
- Slicer 16x16 de `Beginning Fields.png`, dedupe por hash perceptivo + clasificación con
  ollama (hierba/muro/agua/camino), parser `.tmx` (Tiled) para el mapa virtual, y
  cuantizador EHB (k-means half-aware). Se sustituye el banco de tiles generado por el
  real sin tocar el algoritmo de scroll.
- Aceptación: la 201 evoca un nivel real (visión/ollama), sin tiles repetidos absurdos, a
  320x224.

### F4 · Extraer al engine (solo cuando F1-F3 estén verdes)
- Sacar del demo a `engine/`: cuantizador/paleta EHB, loader de tileset/mapa (Tiled/PNG),
  y consolidar los contratos de ScrollEngine/Playfield ya usados. No abstraer sobre bugs.
- Aceptación: 201 sigue igual de verde tras extraer (ningún cambio funcional).

### F5 · Demo 202 DPF (+ features del 107)
- Sobre el engine ya extraído. Portar las features del 107 **una por una, cada una con el
  harness como gate**: (1) DPF 3+3 con transparencia, (2) corkscrew, (3) variantes
  conmutables por teclado, (4) telemetría de blits por frame consolidada en el engine.
- Aceptación: cada feature en su propia mini-demo mínima y medible antes de seguir.

## Apéndice · Secundario (no bloquea)
- **Tiles 32x32**: generalizar `ScrollConsts`/bitmap_width/height/blocks-per-row/
  BLOCKPLANELINES/modulo Blitter/saveword, y ampliar `verify-tile-scroll-modes.mjs`.
- **Pasos de scroll 8..16px**: hipótesis = banda de guarda de **≥2 tiles de 16px** por
  cada lado del bucle (columnas/filas de relleno pre-pintadas), igual que `XYLimited` con
  su banda de extra; validar empíricamente 4/8/12/16px con path 8-way (LUT sin divisiones).
- Se abordan cuando 201(EHB)+202(DPF) estén estables.

## Herramientas (crear en `tools/`)
1. **Slicer + dedup**: corta `Beginning Fields.png` en tiles 16x16 (y 32x32 después) y
   genera el banco de bloques; dedupe por **hash perceptivo** (diferencia de canal RGB +
   estructura) y **clasificación con ollama** (`ollama-desc.mjs`): agrupa los tiles por
   tipo (hierba/muro/agua/camino/decoración) para construir el mapa sin duplicados.
2. **Parser de Tiled (.tmx)**: lee `Beginning Fields.tmx` (objetos, layers, `tile id`) y
   resuelve los PNG/tilesets de los subdirectorios del pack → exporta a C (`u16 cells[]`)
   o al `dh0` del Amiga para la demo.
3. **Cuantizador EHB** (ver algoritmo) → genera `COLOR00..31` + plano half (bit EHB en
   BPLCON0) y un banco ya cuantizado.
4. **Verify**: ampliar `bands.mjs` (negro inferior), `view-frames.mjs`, `ollama-desc.mjs`
   y el harness como gate (`verify-harness.mjs --strict-fps` cuando proceda).

## Algoritmo EHB (recortes de lo interesante del plan original)
- Solo 32 registros base; el set efectivo es {base₀..base₃₁} ∪ {half(base₀)..half(base₃₁)}
  con `half(c) = c>>1` por componente. **Reparto clave**: si eliges el blanco `$FFF`, el
  half te da `$777` automáticamente → no se "gasta" un base extra por un gris que ya
  aporta su half. El color 0 se reserva para transparencia.

**Algoritmo 1 · k-means modificado (el más citado en EAB, recomendado)**:
- Los "centros" son solo los 32 bases. Al asignar un píxel se toma
  `dist = min(dist(p, base), dist(p, half(base)))`, anotando si eligió full u half.
- Tras asignar, cada centro se recalcula SOLO con sus píxeles (a los half se les sube ×2
  antes de promediar, o se promedia en el espacio "claro", con peso/posición adecuada).
- Iterar hasta convergencia. Después remap + Floyd–Steinberg en el espacio half-aware
  (emisión al plano full/half elegido con el bit EHB).

**Algoritmo 2 · Dos mitades + emparejamiento (heurística rápida)**:
1. Ordenar todos los colores de la imagen por luminosidad.
2. Cuantizar la mitad más clara a 32 colores (median-cut/octree/k-means).
3. Cuantizar la mitad más oscura a 32 colores.
4. Emparejar cada color claro con el oscuro más cercano (subiendo el oscuro ×2 o bajando
   el claro /2) y el base final es una media ponderada que minimice el error de ambos.

**Algoritmo 3 · Búsqueda local / brute-force (calidad alta, lento)**:
- Probar variantes de los 32 bases y quedarse con la de menor error total tras remapeo +
  dithering (idea usada en ham_convert y partes de png2amiga).

**Alternativa externa**: clonar/compilar `https://github.com/tinic/png2amiga` en
`C:\Users\dvdjg\Documents\programa\AI\Amiga\png2amiga` como referencia de salida
(interleaved), o implementar el propio (preferible: respeta nuestro registro EHB y la
reserva de transparencia). El objetivo: elegir 32 bases de forma que
{base}∪{half(base)} se acerque lo máximo a la imagen minimizando el error total.

## Lección aprendida · convención de índices del tilebank EHB (2026-09-02)

`slice-tiles.mjs` vuelca el banco crudo con los índices **intercalados** `paletteI`:
`[base₀, half₀, base₁, half₁, ...]`, es decir índice par `2k` = base del color `k` e
índice impar `2k+1` = half del color `k`, y `palette.json`/`kEhbPalette[64*3]` siguen ese
orden intercalado. Pero el chipset EHB espera **bases-primero**: los 32 registros `COLOR0..31`
son las bases y los índices `32..63` (bit 5 = plano 6) generan el half como `base/2`.

El desajuste (sumado a cargar el array intercalado completo en `COLOR0..31`) hacía que la
demo 201 mostrara el mapa con pocos colores / oscuro y desplazado frente a
`reconstruct.png`. Corrección en `demos/201_ehb_map/src/main.cpp`:

- **Paleta**: cargar solo las 32 bases en `COLOR0..31`, tomando los términos pares de
  `kEhbPalette` (`palette[i] = kEhbPalette[2*i]`); el half lo genera el hardware.
- **Plano half**: convertir el índice intercalado `v` del banco a índice EHB
  `e = (v>>1) | ((v&1)<<5)` antes de componer las planes (`fill_planes`): la base `2k`
  va a `k` y el half `2k+1` a `32+k`.

Verificación: histograma de la captura real (`out/run/201_ehb_map/A500_debug/screenshot.png`)
coincide con `reconstruct.png` (verde dominante `143,213,129` ↔ `136,221,136`, marrón/tan,
verdes secundarios), confirmado además con ollama (`tools/analyze/ollama-desc.mjs`).

**Ojo para el pipeline en F3/F4**: conviene que el slicer emita los índices ya en convención
EHB bases-primero (base `k` y half `32+k`) para que el banco crudo sea usable directamente
(blitter) sin conversión por píxel; la conversión por píxel actual es correcta pero añade
coste de CPU que no haría falta.

## Riesgos y decisiones abiertas
- **Throughput del emulador**: mientras cap ~11fps no se puede exigir `--strict-fps`; hay
  que subirlo (apagar tracing `%TEMP%\winuae-gdb.log`, revisar CPU/máquina) antes de gate
  fps estricto.
- **Guardas con pasos grandes**: confirmar empíricamente ≥2 tiles de 16px por lado (o
  equivalente con tile 32) antes de cerrar el apéndice.
- **Chip RAM**: el tileset real (más planos y colores) y el mapa virtual deben caber;
  re-medir tras F3.
- **Mapa real vs duplicados**: la dedup por hash perceptivo + clasificación ollama debe
  garantizar bloques visualmente distintos y representativos (visión/ollama como gate).
- **EHB y coste**: los blits son de 1 plano; el half es del hardware → el EHB no cambia
  el algoritmo de scroll (solo la cuantización). Confirmar en F2.

## Criterio global de "done" (antes del 202/DPF)
Harness verde, `INK==0` y `bands` sin negro en todas las fases, 8-way 1..4px impecable,
201 EHB con mapa real evocador a 320x224, código extraído al engine sin cambios
funcionales.