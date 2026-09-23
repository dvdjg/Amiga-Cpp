# Test HOST-027: animaciones deterministas (contenido desacoplado)

Respalda `eng::graphics::Animation` / `Frame` (`engine/include/eng/graphics/
animation.hpp`): secuencia de frames con avance por **tiempo de juego fijo** (ticks),
bucle o one-shot. La **representación** (sprite hardware / BOB / CPU / playfield) no
participa: el contenido es el mismo.

Se comprueban: avance exacto por ticks, bucle, one-shot que termina en el último
frame, determinismo, equivalencia de un tick grande frente a ticks de 1, y animación
vacía.

```bash
bash tools/run-host-tests.sh tests/host/027_animation
```

Contexto: `docs/engine/architecture/CONTENT_AND_TILEMAP.md` §3.
