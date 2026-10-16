# HOST-452 — playfield_probe

Test host de la **sonda de píxel del playfield** (`engine/include/eng/field/playfield_probe.hpp`):
la utilidad de verificación de render extraída del auto-chequeo de la demo 121 — leer el
índice de color de un píxel VISIBLE de un playfield corkscrew a partir de su
`PlayfieldHardwareView`, sin que el llamador conozca la geometría del anillo.

## Qué cubre

- **`read_visible_pixel`**: línea del anillo `(display_offset + sy) % display_height`
  (la ventana visible empieza en `display_offset`; el resto es guarda), layout intercalado
  del corkscrew (`fila·planos + plano`), MSB = píxel izquierdo, bit `p` del índice = plano
  `p`. Verificado sobre un anillo sintético de 3 planos: píxeles en la primera/última
  columna del byte, plano alto, **wrap de la guarda** y límites (viewport, buffer nulo,
  `planes`/`display_height` cero).
- **`read_confirm`**: re-lectura de confirmación contra `expected` que corta en cuanto el
  valor es correcto (una lectura en el caso bueno) y agota `passes` devolviendo el último
  valor en un fallo real; cubre el patrón de lecturas transitorias del auto-chequeo de la
  demo 121 (un plano obsoleto en la primera pasada).
- **`read_visible_pixel_confirm`**: atajo sobre la vista.

## Ejecución

```bash
bash tools/run-host-tests.sh tests/host/graphics/452_playfield_probe
```

## Estado

La geometría real (ventana del corkscrew con `display_offset`, guardas superior/inferior)
se ejercita en hardware en la demo 121, que además comprueba la invariante
`display_offset == kRingGuardY` en su init; este test cubre la matemática y los límites en
host.
