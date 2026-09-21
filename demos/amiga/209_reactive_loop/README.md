# 209 — bucle reactivo del mini-SO sobre `eng::App`

Demuestra el **bucle reactivo** del mini-SO (`eng::os`) integrado con el `Engine` y la fachada
`eng::App`: el juego no sondea hardware, consume **mensajes** de un puerto.

- **VBlank**: el `Engine` llama a su **hook de VBlank** (`Engine::set_vblank_hook`) una vez por
  tick; el `App` lo usa para publicar `MsgType::VBlank` en su puerto (sin abrir un segundo
  servicio de VBlank).
- **BlitDone**: `App::blitter_memcpy_async` arranca una copia por Blitter y, al terminar la IRQ
  BLIT, publica `MsgType::BlitDone`.
- El juego drena `app.port()` en `update`, **verifica la copia** al recibir `BlitDone` y muestra
  `reactive loop: OK`.

```
   bash ./tools/build/build-demo.sh demos/amiga/209_reactive_loop --debug
   bash ./tools/run/run-demo.sh demos/amiga/209_reactive_loop --warp
```

## Estado: verificado

Host: `tests/host/251_reactive_loop` (hook de VBlank, consumo, `vblank_count`/`blitdone_count`).
Demo: captura con `VBlank: OK` y `BlitDone: OK`.

## Referencias

- `docs/engine/architecture/MINI_OS_MESSAGE_LOOP.md` §6–§7.
- `engine/include/eng/engine.hpp` (`VBlankHook`, `InterruptTick`).
- `engine/include/eng/api/game.hpp` (`App::port`/`pump`/`blitter_memcpy_async`).
