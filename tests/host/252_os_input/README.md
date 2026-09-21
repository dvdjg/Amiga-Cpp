# HOST-252: productores de entrada del mini-SO (`eng::os`)

Test host de los productores de entrada (`engine/include/eng/os/input.hpp`): convierten el estado ya
leído del hardware en mensajes, **solo cuando cambia**.

## Qué comprueba

1. `JoyProducer`: la primera muestra emite; un estado igual **no** emite; cambio de fuego o de
   dirección emite (`MsgType::Joystick` con puerto/dirs/fuego).
2. `PadProducer`: emite `Gamepad` solo si cambia el bitmask.
3. `MouseProducer`: compara contadores de 8 bits, mantiene la posición absoluta **clampada** y emite
   `MouseMove` (si se movió) o `MouseButton` (si cambió el botón).

## Salida de referencia

```
OK: productores de entrada (joystick, gamepad, raton) validados.
```

## Ejecutar

```bash
bash tools/run-host-tests.sh tests/host/252_os_input
```
