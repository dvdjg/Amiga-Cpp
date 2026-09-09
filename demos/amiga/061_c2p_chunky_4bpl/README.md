# Demo 061: c2p_1x1_4 — chunky 4bpp → planar (Oleada 1 de libgfx)

Valida en hardware la primera pieza portada de `libgfx` de `demoscene-repo-orig`:
`eng::graphics::c2p_1x1_4`, que convierte un buffer **chunky** (1 byte por pixel,
4 bits de color en el nibble bajo) al formato **planar** de 4 bitplanes que lee el
DMA de Agnus.

## Qué hace

1. Genera un buffer chunky 160x128 determinista:
   - filas 0..63: banda horizontal, índice = `(fila/4) % 16` (cada 4 filas un gris
     distinto), para validar que cada fila de chunky aterriza en su fila planar
     correcta;
   - filas 64..127: cuadrícula `(col/4 + fila/4) & 15`, para validar la conversión
     por píxel.
2. Llama a `eng::graphics::c2p_1x1_4(...)` que desintercala los 4 planos.
3. Publica los 4 planos en el primer plano de un `StaticEhbScene` (6 planos EHB;
   los planos 5/6 quedan a 0 para no activar half-brite).
4. Paleta RGB444: índice `i` → `i*0x111` (rampa de grises), para que el análisis
   verifique el c2p comparando índices contra colores.

## Implementación del c2p

- `engine/include/eng/graphics/c2p.hpp` — **versión C++ naive** (O(4·w·h)),
  correcta por construcción y testeable en host. Suficiente para efectos CPU de
  tamaño modesto a 50 fps.
- `support/c2p_1x1_4.s` — el **c2p de Kalms/Scout** (1999), port a GAS con la ABI
  del repo, conservado como rutina optimizada (writes de word, tres fases de
  máscara `$0f0f0f0f`/`$00ff00ff`/`$55555555` + `$33333333`). El build ya ensambla
  todos los `*.s` de `support/`. Queda como hot path pendiente de validar
  equivalencia con la versión C++.

## Cómo se ve

No se dibuja texto; el resultado es puramente visual (patrón de grises). El
`StaticEhbScene` muestra el área chunky convertida en la esquina superior
izquierda y el resto queda a COLOR00 = negro.

## Build & run & analyze

```bash
tools/build/build-demo.sh demos/amiga/061_c2p_chunky_4bpl --clean
tools/run/run-demo.sh       demos/amiga/061_c2p_chunky_4bpl
tools/analyze/analyze-demo.sh demos/amiga/061_c2p_chunky_4bpl
```

## Criterio de aceptación

- Compila y llega a `Ready` por canal lateral (estado 3).
- `g_eng_run_status.detail = 0x06100000 | 160`.
- En la captura: **rampa de grises** en la zona superior (16 tonos de la cuadrícula)
  y bandas horizontales de gris progresivo; el análisis de píxeles confirma los
  grises `17,34,51,...255` (índices `i*0x111`) distribuidos por fila/columna.

## Relación con la ingesta

Primer entregable de la Oleada 1 (`libgfx`) sobre la nueva estructura del engine.
Ver `docs/demos/effects/OLEADA1_LIBGFX_INVENTARIO.md`.