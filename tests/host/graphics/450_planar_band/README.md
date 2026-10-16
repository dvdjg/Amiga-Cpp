# HOST-450 — planar_band

Test host de **bandas planares** (`engine/include/eng/graphics/planar_band.hpp`): el
rectángulo de bitplanes como bloque de palabras que usan las demos con display indexado
para parchear el buffer mostrado **sin C2P por frame** (actores, cursores, HUD).

## Qué cubre

- **Dimensiones**: `band_window_words` (ventana de un patrón con desplazamiento 0..15) y
  `band_words` (layout `[plano][fila][palabra]`).
- **`make_band_from_pixels`**: conversión patrón de píxeles → banda planar (bit `p` del
  índice → plano `p`, bit 15 = píxel izquierdo) para los desplazamientos (0 y 3): palabras
  y máscara exactas, resto a cero.
- **`get_band`/`put_band`**: extracción y escritura de un rectángulo contra un bitmap
  planar sintético de 3 planos (palabra/fila/plano correctos; nada fuera del rectángulo).
- **`put_band_masked`** (cookie-cut planar): la ley `(dst & ~mask) | (src & mask)`
  verificada contra una **referencia bit a bit independiente**; con la máscara combinada
  de la demo 121 (`actor_mask & ~canopy_mask`), los píxeles bajo la copa conservan el
  fondo y el resto se pinta con el actor; máscara 0 deja el destino intacto.

## Ejecución

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/graphics/450_planar_band
```

## Estado

La variante de display (`IndexedDisplay::capture_band`/`restore_band`/`patch_band`, que
aplica estas funciones al buffer que el Copper muestra) se ejercita en hardware en la demo
`demos/techniques/amiga/effects/121_proc_corner_layers` (actor que pasa bajo las copas);
este test cubre la matemática de bandas en host.
