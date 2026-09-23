# HOST-054 — sombreado por cara (`eng::math::light_ops`) e `hi16`

Fija el punto de personalización del sombreado del culling de `lib3d` (la parte que
convierte `normal·vista` en un color 0..15) y la operación `hi16`.

## Qué cubre

- **`hi16`**: parte alta de 32 bits como `s16`, con truncado de signo (`hi16(-1) == -1`).
- **`light_ops<>::shade`** y el cuerpo portable `shade_portable`: iguales a una referencia
  escrita a mano con enteros **sin signo** en una rejilla de `v` y `e1_sq` (incluye `v < 0`
  y el clamp del índice a 511). La referencia sin signo es lo que garantiza el mismo
  resultado que el `mulu.w` del 68000 (un `mulu` con signo daría otro valor).

## Ejecución

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/core/054_light_ops
```
