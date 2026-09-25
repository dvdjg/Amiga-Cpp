# HOST-324 — sprite cocinado (`eng::graphics::Sprite`)

Respalda `engine/include/eng/graphics/sprite_asset.hpp`: la capa de juego sobre `graphics::bob`.
Valida que `Sprite` reenvía la geometría del `Bob` (`width`/`height`/`planes`/`frames`/
`layout`/`draw mode` y `valid`) y que `draw`/`erase` producen los `BlitJob`s correctos en el
`FramePlan`:

- planar **cookie-cut** (`MaskedBobCookieCut`, minterm `$CA`, una fila por plano de bitplane);
- planar **OR** con desplazamiento fino (`source_shift = x & 15`, palabras `base + guarda`);
- **interleaved OR** (un job, `height = alto × planos`, un solo plano lógico);
- **borrado por caja** (`minterm $0`);
- rechazo de **frame fuera de rango** (sin job) y de cookie-cut interleaved (no lo cubre el
  ejecutor, documentado en `bob.hpp`).

La geometría de fondo (contrato de la hoja, módulos, mintern) ya está cubierta por
HOST-072 (`actor`); aquí se valida la capa `Sprite` que la expone.

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/graphics/324_sprite
```

Relacionado: `docs/reference/amiga/techniques/interleaved-bob-single-blit.md` y
`docs/engine/architecture/PUBLIC_GAME_API.md` §2.1.1.
