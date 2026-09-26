# HOST-338 — decode 2bpp → planar (`eng::graphics::chr_to_planar`)

Respalda `engine/include/eng/graphics/chr.hpp`: convierte un tile **2bpp** (formato NES/Game Boy:
`planes` bloques contiguos de `h*(w/8)` bytes, MSB = píxel izquierdo) a **planos contiguos** de
Amiga (`plane_stride` entre planos, `row_bytes` por fila). Es el helper general para que un
consumidor externo (emulador) implemente `IPatternCache::define` / `ISpriteEngine::define` sin
tocar el intercalado planar. Cubre:

- copia de planos y offset `plane_stride`;
- filas de 16 px (`row_bytes=2`) y 3 planos;
- rechazo de argumentos inválidos (ancho no múltiplo de 8, puntero nulo).

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/graphics/338_chr
```

Ver `docs/engine/architecture/ROADMAP_API_COHERENCE.md` §7 (F7, consumidores externos).
