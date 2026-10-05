# Referencia — entrada

Los **productores de entrada** del mini-SO (`os/input.hpp`) convierten el estado ya leído del hardware en `Msg` de entrada, **solo cuando cambia** (flancos/estado), sin sondear en el bucle. La lectura de registros (CIA/`JOYxDAT`/`POTGO`) la hace el backend; aquí está la parte **pura** y host-testable.

Cada productor guarda su estado previo: en `update` devuelve `true` y rellena el `Msg` si hay novedad (movimiento, cambio de botones o dirección). Un registro que no cambia **no** genera mensaje.

| Productor | Qué emite |
|---|---|
| `JoyProducer` (`input.hpp:19`) | `Joystick` (digital, 1 botón) solo si cambian dirs/fuego. |
| `PadProducer` (`input.hpp:70`) | `Gamepad` (CD32/multi-botón) por el protocolo serie del puerto 2. |
| `MouseProducer` (`:92`) | `MouseMove`/`MouseButton`. |
| `KeyProducer` (`:144`) | `KeyDown`/`KeyUp` (scancodes por CIA-A). |

## Pad CD32

`Cd32Btn` (`input.hpp:42`) es el bitmask **estable** para la app, independiente del orden del stream: `Cd32Blue`/`Red`/`Yellow`/`Green` (botones), `Cd32Forward`/`Reverse` (hombros) y `Cd32Play`. `cd32_mask_from_shift(bits)` (`:57`) decodifica los **8 bits serie** del pad al bitmask. Ver `docs/engine/architecture/MINI_OS_INPUT.md` §6.

Volver al [índice de `os/`](README.md) · [Referencia](../README.md) · [manual](../../README.md).
