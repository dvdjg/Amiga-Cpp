# HOST-313 — Texto por Blitter con caché de glifos

## Qué cubre

`engine/include/eng/graphics/glyph_cache.hpp`:
- `GlyphMask`: máscara planar de 1 bit (8 filas) de un glifo de `Font8` (MSB = izquierda).
- `GlyphCache<Max>`: caché de capacidad fija (sin heap) de máscaras por *code point*.
- `draw_text_blit(...)`: dibuja texto UTF-8 con **cookie-cut del Blitter** (`MaskedBobCookieCut`,
  minterm `$CA`). Con `x` alineado a 16 px agrupa **dos glifos por palabra** (2 glifos/blit); con
  `x` no alineado pre-desplaza el par a **dos palabras** y emite desde `x & ~15` (blit de 32 px),
  manteniendo la equivalencia píxel a píxel con la CPU.
- `src_scratch`/`mask_scratch` son buffers del **llamador** que persisten hasta ejecutar el
  `FramePlan` (el encolado del job solo guarda punteros: un buffer local sería pila muerta).

## Invariantes

1. La máscara del glifo coincide con `Font8` (bit `k` de la fila → columna `k`, MSB primero).
2. `draw_text_blit` produce el **mismo resultado píxel a píxel** que la ruta CPU
   `Surface::draw_text` (referencia canónica de equivalencia), tanto con `x` alineado como no.
3. Texto de longitud impar: la segunda mitad de la última palabra conserva el fondo.
4. `x` no alineado a palabra: el par pre-desplazado a 2 palabras == CPU píxel a píxel.
5. Con `BlitterRaster`, la **geometría del job** encolado es la esperada: `words_per_row` 1
   (alineado) o 2 (no alineado), `source_plane_stride_bytes = kRows*2` o `kRows*4`, un job por plano.

## Ejecución

```bash
bash tools/run-host-tests.sh tests/host/ui/313_ui_text_blit
```

## Relación

- Diseño: `docs/engine/architecture/GUI_LIBRARY.md` §6.
- Protocolo: etapa 4 (sustitución por hardware con equivalencia CPU) de
  `docs/guides/methodology/PROTOCOLO_ETAPAS_GRAFICOS.md`.
- Ruta CPU de referencia: `Surface::draw_text`/`draw_code_point` (`eng/field/surface.hpp`).
- Uso desde la UI: `UiPainter::text_blit` (`eng/ui/painter.hpp`). La ruta de hardware
  (Blitter real) se valida en la demo de GUI en emulador.
