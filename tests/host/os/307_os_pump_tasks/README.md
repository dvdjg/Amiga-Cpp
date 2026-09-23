# HOST-307 - os_pump_tasks

Test host de la **integración del fondo en el bucle de mensajes**:
`eng/os/message_pump.hpp` (`MessagePumpGame`) + `eng/os/task.hpp` (`TaskSystem`). Cierra el
pendiente de M10 (`ROADMAP_MINI_OS.md`): el `run_idle` se dispara desde el bucle.

## Qué valida

- **Idle solo sin mensajes**: si el frame entregó mensajes al `App`, el fondo **no** avanza; si no
  hubo mensajes, se da **un slice** de idle (`run_idle`). Así el frame y la entrada siempre ganan.
- **`preempt`**: un productor puede pedir `preempt`; el bucle no da idle ese frame y **limpia** la
  marca, de modo que el siguiente frame sin mensajes vuelve a dar slice.
- **Sin `TaskSystem`**: el bucle se comporta como antes (drena, `on_frame`).

## API

`MessagePumpGame` añade `bind_tasks(TaskSystem&)` y `idle_slice_us`; `pump_messages` devuelve el
número de mensajes entregados (para decidir el idle).

## Build / run

```
CXX=<g++> bash tools/run-host-tests.sh tests/host/os/307_os_pump_tasks
```
