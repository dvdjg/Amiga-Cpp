# HOST-485 — `eng::scene::SpriteDirector`

Test host de la fachada por intención de sprites (`engine/include/eng/scene/sprite_director.hpp`) y de su contrato de contabilidad (`sprite_visual.hpp`).

Cubre:

- `VisualStore`/`CompositeStore`/`SceneAssets`: handles generacionales, baja y reciclaje (un id viejo deja de ser válido).
- `SpriteDirector`: alta de contenido simple y compuesto, `SpriteVisualRef` sin punteros crudos, `tick` y estado compuesto.
- Composición combinada: primero `CompositeScene` y después `SpriteScene` con un `SpriteChannelLedger` compartido; placements y eventos de paleta combinados en una sola presentación.
- `SpriteAllocator` con gap de 1 línea: dos sprites contiguos no comparten canal; un par *attached* publica dos placements (canal par + impar con `ATTACH`).
- Tira horizontal (`strip_id`/`strip_index`/`strip_span`) y política `Bob` forzada (`ActorDesc::force_bob`), con coste de Blitter medido por delta del `FramePlan`.
- `BandPalette`: `top + scroll_y`, playfield a `copper::Plan` y sprites como `SpritePaletteEvent`; el test materializa el Plan y comprueba que no hay overflow.
- `SpriteLoadReport`: canales, scanline, BOB, Copper, paleta, emisores y coste.
- `evaluate_static` en `constexpr`/`static_assert` con el mismo gap; presupuesto insuficiente marca aviso y `ok == false`.
- `present` combinado: placements + paletas + cadenas opcionales; la copperlist se emite sin overflow.
- `refresh_positions` con animación de dos frames: actualiza X/Y, alto y la DATA del frame vigente; un par *attached* se recocina al cambiar de frame y mantiene sus dos placements/ATTACH.
- `SpriteDirectorPass`: reenvía placements y plan de BOB al host, usa `refresh_positions` cuando solo cambian posiciones y `request_emit` fuerza una emisión completa. No decide representación ni z-order: no es una capa.

## Ejecución

```bash
bash tools/run-host-tests.sh tests/host/scene/485_sprite_director
```
