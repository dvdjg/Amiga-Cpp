# HOST-466 - `frame_index` vs `field_index` (latido VBlank)

Test host del contrato de contadores del bucle por IRQ (`eng::VBlankHeartbeat`, `eng/engine.hpp`, `eng/graphics/driver.hpp`):

- `frame_index` cuenta **frames completados** (lo fija el bucle principal: `run_frames` y `run_frames_polling`); es lo que leen las animaciones, el run-status y el HUD.
- `field_index` cuenta **latidos VBlank** (lo fija la IRQ del heartbeat); es el pacing de los servicios de fondo (`run_slice`).
- La IRQ **no** debe pisar `frame_index`: hacerlo metía el contador de campos (50/s) en el de frames a mitad de frame — caso real 2026-10: el HUD de 079/116 mostraba ~50 fps (49.2) cuando la tasa real era 32.7; el contador de frames saltaba ×1,5.

Pieza sin hardware: se instancia el heartbeat real con backend/juego dummy y se comprueba que tras N latidos `field_index == N-1`, `frame_index` queda intacto y el hook corre una vez por latido.

## Ejecución

```bash
bash tools/run-host-tests.sh tests/host/os/466_heartbeat_frame_index   # solo este
bash tools/run-host-tests.sh                                          # todos
```
