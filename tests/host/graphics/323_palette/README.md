# HOST-323 — paleta de juego (`eng::Palette`, `Color`, `ColorIndex`)

Respalda `engine/include/eng/graphics/palette.hpp`: la capa de ergonomía de paleta que
faltaba sobre `Palette32` y `eng::util`. Cubre:

- los tipos fuertes `Color` (RGB444, con `rgb()` que enmascara cada canal a 4 bits) y
  `ColorIndex` (`0..31`), y `color_index()` que recorta el índice sobrante;
- `Palette::set`/`get`/`fill`/`copy_from` (array y `PaletteWords`);
- `fade`/`fade_from` (fundido `num/den`, reutiliza `util::palette_scale`) y `mix` (tramo
  parcial, reutiliza `util::palette_lerp`), con el origen intacto;
- `apply(FramePlan&)`, que registra el **parche base** de paleta (`first`/`count`),
  materializado luego por el driver;
- `words()`/`operator PaletteWords` para las APIs que piden la vista de dominio.

Como `set`/`fill`/`fade`/`mix` son `constexpr`, el test incluye `static_assert` que
verifican la aritmética **en tiempo de compilación** (además de la pasada runtime).

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/graphics/323_palette
```

Relacionado: HOST-133 (`palette_transition`, el efecto de transición que comparte el
mecanismo del parche de paleta) y `docs/engine/architecture/PUBLIC_GAME_API.md` §2.1.2.
