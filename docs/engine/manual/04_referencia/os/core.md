# Referencia — fachada, tiempo y timers

La **fachada** del mini-SO (`os/os.hpp`) y los servicios de tiempo. La implementación de `tick`/`system_port` la aporta el backend; el resto es portable.

## Fachada — `os/os.hpp`

| Símbolo | Qué hace |
|---|---|
| `system_port()` (`os.hpp:18`) | El `MsgPort<32>` de la aplicación (lo implementa el backend). |
| `frame_count()` (`os.hpp:21`) | Contador de frames (VBlank). |
| `tick()` | **Tick del mini-SO**: incrementa el frame, marca el `VBlankLatch` (`SigVBlank`) y **pollea** los productores de entrada posteando los mensajes que cambien. Lo llama el bucle principal una vez por frame. |
| `wait(mask)` (`os.hpp:32`) | Espera a que haya alguna señal de `mask` y devuelve los bits ya listos (consumidos). No bloquea con teclado/ratón apagados: si no llega nada, gira en `tick()`. |
| `post_user(code, a, b)` / `request_quit()` | Postean `MsgType::User` / `MsgType::Quit`; seguros desde cualquier sitio. |
| `enable_keyboard()` / `enable_cd32_pad()` | Habilita el teclado (CIA-A, IRQ nivel 2) / el pad CD32 (puerto 2, `POTGO`/`POTINP`; sin pad, `tick` cae al joystick). |
| `InputMask`/`start_vblank_irq`/`init` (`os.hpp:53`, `:89`, `:104`) | Dispositivos que `input_enable` activa y el arranque del bucle. |

## Tiempo — `os/time.hpp`

`kCiaKHzPal = 709` / `kCiaKHzNtsc = 715` (`time.hpp:17`) son las frecuencias del CIA. `ticks_to_us`/`us_to_ticks` (`:22`, `:28`) convierten; `TickSource` (`:35`) da el origen de ticks y `ScopedTimer` (`:43`) mide un tramo. El acceso a los registros lo aporta el backend.

## Timers — `os/timer.hpp`

`TimerService` (`timer.hpp:35`) son timers de usuario en **frames** (sobre el VBlank) o en **µs** (sobre los ticks del CIA) que postean `MsgType::Timer` con su id. Se pollean una vez por VBlank; **no** se ejecuta lógica del juego en la ISR. `TimerSlot` (`:23`) guarda `active`/`periodic`/`unit`/`id`/`deadline`/`period`; `kMaxTimers = 16` (`:32`). `start(id, delay, unit, periodic, frame_now, ticks_now)` (`:40`) arranca (id 0 = autoasignado). Es **puro**: `poll_and_post` recibe los tiempos como parámetros (host-testable con ticks sintéticos).

## Telemetría — `os/telemetry.hpp`

`IrqTelemetry` (`telemetry.hpp:19`) acumula los **descartes por cola llena** y la saturación del mini-SO.

Volver al [índice de `os/`](README.md) · [Referencia](../README.md) · [manual](../../README.md).
