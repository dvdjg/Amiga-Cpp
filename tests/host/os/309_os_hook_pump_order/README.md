# HOST-309 — Orden hook de VBlank → pump

Fija el **contrato de orden** del bucle de polling del `Engine`: el hook de VBlank
(`os::vblank_hook`, que ejecuta el latido del mini-SO y `TimerService::poll_and_post`) corre
**antes** de `Game::update`, y `MessagePumpGame::update` drena el puerto. Por tanto un `Timer`
posteado por el hook debe entregarse en el **mismo** `update`.

Cubre:

- periodo 1: un `Timer` por frame (10 en 10 frames);
- **periodo 2**: un `Timer` cada 2 frames (5 en 10 frames) — el caso que en hardware no llega
  (bug abierto, ver `docs/guides/roadmap/ROADMAP_MINI_OS.md`);
- dos timers con periodos distintos, entregados en su frame;
- un `post` del juego previo al `update` se entrega en ese `update`.

Es un test de **contrato puro** (sin backend): valida que `poll_and_post` + `pump_messages`
entregan correctamente cuando se llaman en el orden del bucle real. La discrepancia con hardware
apunta a la interacción hook↔pump en el backend Amiga, no a `TimerService`.

```bash
bash tools/run-host-tests.sh tests/host/os/309_os_hook_pump_order
```
