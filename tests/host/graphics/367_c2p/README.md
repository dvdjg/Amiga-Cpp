# Test HOST-367: `eng::graphics::c2p` (conversión con despacho por banco)

La conversión **chunky→planar** como **función** sobre memoria **etiquetada con su banco**:
`c2p(chunky, planes, w, h, stride, planes_count, blit_submit)`. El **tipo** de las vistas decide
la vía (regla «los tags eligen el método»):

- **Ambas en Chip** → delega en el **Blitter** (por `blit_submit`, normalmente `FramePlan::add_c2p`).
- **Alguna fuera de Chip** (Fast/Slow) → **CPU** (`c2p_1x1_4`, o `c2p_1x1_naive` para 1..6 planos),
  porque Agnus no ve esa memoria.

## Respalda

| Cabecera | Qué |
|---|---|
| [engine/include/eng/graphics/c2p.hpp](../../../../engine/include/eng/graphics/c2p.hpp) | `c2p_1x1_4`/`c2p_1x1_naive` (CPU) y el despachador `c2p(...)` |

## Comprueba

- Con fuente/destino **no-Chip**, `c2p` va por **CPU** y **no** llama al `blit_submit`.
- Con **ambas Chip**, `c2p` **delega** en el `blit_submit` (una vez) y **no** ejecuta la CPU.

El patrón planar exacto de Kalms lo fija la demo **061_c2p_chunky_4bpl** (contra el asm).

## Ejecución

```
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/graphics/367_c2p
```
