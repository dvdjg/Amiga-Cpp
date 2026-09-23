# HOST-251 — bucle reactivo del mini-SO sobre `App`

Valida el bucle reactivo que integra el mini-SO (`eng::os`) con el `Engine` y la fachada
`eng::App` (`eng/engine.hpp`, `eng/api/game.hpp`):

- **Hook de VBlank** (`Engine::set_vblank_hook`): se llama una vez por frame (interrupt-driven y
  polling) y el `App` lo usa para publicar `MsgType::VBlank`.
- **Consumo en `update`**: el juego drena `app.port()` y `app.vblank_count()` refleja los frames.
- **Blit asíncrono**: `App::blitter_memcpy_async` publica `MsgType::BlitDone` (IRQ BLIT) y sube
  `app.blitdone_count()`.

## Ejecución

```
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/251_reactive_loop
```

Diseño canónico: `docs/engine/architecture/MINI_OS_MESSAGE_LOOP.md` §6–§7.
