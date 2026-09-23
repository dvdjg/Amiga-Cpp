# HOST-220: puente `Msg` → `UiEvent` (`eng::ui`)

Test host del puente del mini-SO a la UI (`engine/include/eng/ui/ui_bridge.hpp`): la UI no lee
hardware; consume `UiEvent`.

## Qué comprueba

1. `MouseMove`/`MouseButton` → `UiEvent` con posición y botones (`MouseDown`/`MouseUp` por el bit 0).
2. `KeyDown`/`KeyUp` → `UiEvent` con scancode y **modificadores** (Shift/Ctrl/Alt/Amiga).
3. `Joystick`/`Gamepad` → `JoyButton` con puerto y bitmask.
4. Los tipos que **no** son de entrada (VBlank, Timer, FileDone, User, Quit) se descartan.

## Salida de referencia

```
OK: puente Msg->UiEvent (raton, teclado, joystick, descartes) validado.
```

## Ejecutar

```bash
bash tools/run-host-tests.sh tests/host/os/220_ui_bridge
```
