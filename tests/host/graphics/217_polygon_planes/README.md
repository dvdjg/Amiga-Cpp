# HOST-217: relleno de polígonos compuesto por bitplane (CPU)

Test host de `eng/graphics/polygon_planes.hpp`: el **relleno compuesto por bitplane**. El
Blitter rellena máscaras de 1 bit y el color de un polígono es un patrón de bits en los
planos, así que en vez de rellenar polígono a polígono (outline + fill + aplicar a los planos
de su color) se rellena **por plano**: para cada plano `p`, la unión de las caras cuyo color
tiene el bit `p` a 1. Las **aristas compartidas** por caras con el mismo bit se cancelan
(doble cruce even-odd); donde los bits difieren, la arista es frontera real.

## Qué comprueba

1. **Mismo color** (1) en dos triángulos que forman un cuadrado: el plano 0 rellena la
   **unión** (el cuadrado, con la diagonal compartida cancelada) y el plano 1 queda vacío.
2. **Colores distintos** (1 y 2): el plano 0 rellena solo el triángulo A y el plano 1 solo
   el triángulo B (reparto por bits del color).
3. **`color == 0`** no rellena nada.
4. **Builder** `PlaneFillBuilder<MaxFaces>` (`submit`/`fill_cpu`/`faces`), patrón `SubmitPoly`/`EndFrame`.
5. **Plan de patrón** `FramePlan::add_pattern_fill` (kind `PatternFill`, minterm `$FC`, módulo de A).

Es la referencia CPU del camino Blitter (1 fill por plano en vez de 1 por polígono); el
backend Amiga hace lo mismo con línea + `area fill` + copia por plano, y su self-test en 077
lo verifica en hardware.

## Salida de referencia

```
OK: polygon_planes (relleno compuesto por bitplane, CPU).
```

## Ejecutar

```bash
CXX="/c/.../g++.exe" bash tools/run-host-tests.sh tests/host/graphics/217_polygon_planes
```
