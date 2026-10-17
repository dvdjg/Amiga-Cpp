# HOST-464: generador de parches planares (`eng::graphics/planar_patch.hpp`)

Test host del generador `constexpr` promovido al engine desde las demos RPG (001/400): convierte `pixel(y, x) -> color` en el bloque **interleaved 4bpp por fila de píxel** que el Blitter copia sin máscara ni shift.

## Qué comprueba

1. **Formato exacto**: fila de píxel = `[plano0..plano3]`, bit MSB = píxel izquierdo; índice `(y×4 + plano) × (W/16) + palabra`.
2. **Compilación**: `static_assert` sobre patrones sólidos, bordes y un píxel suelto en parches de 16 y 32 px de ancho (con varias palabras por fila).
3. **Límites en runtime**: color 0 (bloque vacío), planos no seleccionados a cero y bit correcto con posición impar.

## Salida de referencia

```
Planar patch (generador constexpr 4bpp interleaved):
OK: planar patch (layout/limites/compilacion)
```

## Ejecutar

```bash
bash tools/run-host-tests.sh tests/host/graphics/464_planar_patch
```
