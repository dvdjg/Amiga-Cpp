# Roadmap: Scroll 8-way X-Limited correcto (demo 201, EHB) antes de DPF

Fecha: 2026-09-01 · Decisión: **no pasar a DPF hasta que el 8-way X-Limited esté al 100%**
(precisión de tiles, scroll 1..4px, luego 32x32 y pasos hasta 16px, con banda de guarda).

## Objetivo
Construir el motor A500 con una base medible y verificable. El primer hito duro es un
**scroll 8-way con X-Limited impecable**, expuesto en una demo 201 en **EHB** (32 colores
base + 32 half-bright) con un **mapa que evoque un mundo real** (tileset "The Fan-tasy",
slicing 16x16, sin tiles duplicados), mapa virtual y 320x224. Solo cuando esto esté al
100% se reintegra el DPF (las features del 107 se portan una por una, ver Roadmap §R).

## Estado actual (punto de partida)
- El harness mejoró: canal lateral/READY fiable (`tools/debug/verify-harness.mjs`, sin
  fallback). El throughput del emulador (WinUAE-DBG) capa ~11fps para demos cargadas;
  el fps-gate se informa y solo exige con `--strict-fps` (cuando el emulador esté a pleno).
- Bug abierto (debe cerrarse ANTES de cualquier avance): tiles de relleno pintados en la
  zona visible (negro intermitente en la costura del bucle). Diagnóstico en marcha con
  watchpoint `g_eng_diag_hit` (bit24 + fila + display_offset) y breakpoints en
  `add_draw` (xlimited.hpp:860/877), `draw_block_job` (:772) y `scroll_down/up`
  (scroll_engine.hpp:268/318).
- La demo 107 queda **congelada** como laboratorio histórico.

## Fases (ordenadas, cada una con criterio de aceptación MEDIBLE)

### F1 · Cerrar el bug "tile pintado en el área visible" (8-way 1px)
- Capturar con el watchpoint `g_eng_diag_hit` el primer hit y sus `(y, d, row, x0, x, frame)`.
- Determinar la causa: o el índice de inicio del viewport
  (`display_offset = (videoposy+16)%display_height`) o el destino de la columna de
  relleno `x0+bitmap_width` (desborde a la fila siguiente = visible), o del fill en la
  costura envuelta.
- Parchear el destino/offset y verificar con `bands.mjs`: **negro inferior = 0 en todas
  las fases**, e `INK` (`g_eng_diag_hit`) = 0 en todas las fases.
- Aceptación: scroll 8-way 1px sin frames negros ni tiles visibles en ninguna fase del
  bucle (VPOS cycles) + regresión de secuencia sin artefactos.

### F2 · Scroll 1px, 2px y 4px (staging correcto)
- Validar que el fillup/destinos quedan en la banda NO visible con pasos 1/2/4 px.
- Aceptación: para cada paso, `INK==0`, sin negro inferior, y la secuencia muestra un
  mapa real desplazándose con continuidad (visión local + `bands.mjs`).

### F3 · Tiles de 32x32
- Generalizar `ScrollConsts`/bitmap a tile 32x32 (bitmap_width/height, blocks-per-row,
  BLOCKPLANELINES, modulo Blitter, saveword). Mantener el script de descomposición
  (`verify-tile-scroll-modes.mjs`) para 4/5/6 single + dual, ahora también con 32.
- Aceptación: el alineamiento de la costura continúa impecable con tile 32; mapa real al
  50fps medible (harness informativo; `--strict-fps` cuando el emulador permita).

### F4 · Pasos de scroll grandes (hasta 16px) + banda de guarda
- Hipótesis a validar: pasos de 8..16px requieren **banda de guarda de ≥2 tiles (32px
  de tile 16)** por cada lado del bucle (columnas/filas de relleno pre-pintadas), igual
  que `XYLimited` con su banda de extra. Experimentar: `display_height = viewport_h +
  guardY`, `bitmap_width` con guardX, y el número de bloques a re-pintar por cruce.
- Aceptación: pasos de 4/8/12/16px en tile 16 sin gaps, sin tiles en visible, con el
  mapa continuo (path de 8-way con LUT sin divisiones).

### F5 · Demo 201 EHB con mapa real
- Preparación de assets (ver "Herramientas") y cuantización EHB (ver "Algoritmo EHB").
- La demo 201: dos planos en EHB (p. ej. playfield base + logo/glyphs con half-bright),
  mapa virtual con tileset real (16x16), scroll 8-way 1..4px, 320x224, exponiendo
  `g_eng_run_status` + telemetría. Sin corkscrew ni DPF todavía.
- Aceptación: la imagen evoca un nivel real (visión/ollama: "mapa de hierba/muros/camino,
  sin duplicados repetidos"), scroll continuo y estable, y el harness verde.

### F6 · Solo entonces: reincorporar el DPF y features del 107
- Portar cada feature del 107 (DPF 3+3, corkscrew, variantes conmutables) en demos
  mínimas, con el harness como gate de cada una.

## Herramientas (a crear en `tools/`)
1. **Slicer + deduplicación**: corta `Beginning Fields.png` en tiles 16x16 (y 32x32 más
   adelante) y genera el banco de bloques; dedupes por **hash perceptivo** (diferencia de
   canal RGB + estructura) y **clasificación con ollama** (`ollama-desc.mjs`): pide a
   ollama agrupar los tiles por tipo (hierba/muro/agua/camino/decoración) para construir
   el mapa sin duplicados.
2. **Parser de Tiled (.tmx)**: lee `Beginning Fields.tmx` (objetos, layers, `tile id`)
   y resuelve los PNG/tilesets de los subdirectorios del pack → exporta a C (`u16
   cells[]`) o al sistema de archivos del Amiga (dh0) para la demo.
3. **Cuantizador EHB** (ver algoritmo) → genera `COLORxx` (32) + plano half (bit EHB en
   BPLCON0), y un banco ya cuantizado.
4. **Verify**: ampliar `bands.mjs` (negro inferior), `view-frames.mjs`, `ollama-desc.mjs`,
   y el harness como gate (`verify-harness.mjs --strict-fps` cuando proceda).

## Algoritmo EHB (recomendado: k-means modificado, half-aware)
- Input: imagen fuente con alpha (transparencia). El color 0 se reserva para transparente.
- Acotación del chipset: solo 32 registros base; el set efectivo es
  {base₀..base₃₁} ∪ {half(base₀)..half(base₃₁)}, con `half(c)= (c>>1)` por componente.
- Repartir: si eliges blanco `$FFF`, el half te da `$777` automáticamente → el cuantizador
  no debe "gastar" un base extra por un gris que ya aporta un half.
- Pipeline:
  1. `median-cut` inicial a 64 candidatos (mitad clara/mitad oscura para robustez).
  2. `k-means modificado`: asignar cada píxel al base que minimiza
     `dist(p, base)` vs `dist(p, half(base))`, anotando si eligió full u half; recalcular
     cada centro SOLO con sus píxeles (a los half se les sube ×2 antes de promediar, o se
     promedia en el espacio "claro"); iterar hasta converger.
  3. `remap + dithering` Floyd-Steinberg en el espacio half-aware: cada píxel se emite al
     plano (full/half) elegido con el bit half del registro EHB.
  4. Tablas finales: `COLOR00..31`, mapa de half-bits, banco cuantizado.
- Alternativa: clonar/compilar `https://github.com/tinic/png2amiga` como referencia de
  salida (interleaved, paletas) en `C:\Users\dvdjg\Documents\programa\AI\Amiga\png2amiga`;
  preferimos el propio para respetar nuestro registro EHB y la reserva de transparencia.

## Riesgos y decisiones abiertas
- **Throughput del emulador**: mientras cap ~11fps no se puede exigir `--strict-fps`;
  hay que subirlo (apagar tracing `%TEMP%\winuae-gdb.log`, revisar CPU/máquina) antes de
  gatear por fps.
- **Guardas con pasos grandes**: confirmar empíricamente 2 tiles de guarda por lado
  (16px de tile 16) o equivalentes con tile 32.
- **Chip RAM**: el tileset real (más planos y colores) y el mapa virtual deben caber;
  re-medir tras F5.
- **Mapa real vs duplicados**: la dedup por hash perceptivo + clasificación ollama debe
  garantizar que cada bloque es visualmente distinto y representativo (ver F5 visón).

## Criterio global de "done" (antes de DPF)
Harness verde, `INK==0` en todas las fases, `bands` sin negro inferior, scroll
1/2/4px y (con guardas) hasta 16px impecables, tile 16 y 32, y la 201 EHB evocando un
nivel real a 320x224 medible.