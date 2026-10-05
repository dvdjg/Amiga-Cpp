# Referencia — planner de sonido

El audio como **plan análogo** al planner de intenciones (`INTENT_PLANNER.md` §6): el juego `declare()` **sin bloquear**, y `flush()` (en el bucle, **no** en la ISR) drena la cola al plan y postea los mensajes.

## `SoundPlanner<N, PortN>` — `sound_planner.hpp`

Une las tres preocupaciones que antes vivían sueltas (cola, plan y aviso):

- `begin_frame()` (`sound_planner.hpp:52`) limpia el plan (canales + presupuesto).
- `declare(intent)` (`:55`) encola una intención (no bloquea) y devuelve su `Ticket`.
- `flush()` (`:60`) drena la cola al `AudioPlan` (postea `IntentDone` por petición ejecutada) y reporta el flanco de `AudioUnderrun` (**una vez por evento**, no por buffer). Se llama una vez por frame desde el **bucle**.

Se liga con un `AudioMixer` (el sumidero) y un `MsgPort` (no propietarios; deben vivir más que el planner). Ver `docs/engine/architecture/GAME_AUDIO.md` §8 y el test `tests/host/audio/373_sound_planner`.

## Cola y ejecutor — `sound_queue.hpp`

`SoundQueue<N, Executor, Done>` (`sound_queue.hpp:41`) es un alias de `eng::IntentQueue<N, SampleEvent, Executor, Done>`: la **cola** de intenciones de sonido y el **ejecutor** que las aplica. `MixerExecutor` (`:24`) lleva la intención al `AudioMixer`; `Done` es el posteador (por defecto `os::IntentDonePoster`).

## Flancos y stream planner

`audio_events.hpp` detecta los **flancos de eventos** de audio (fase A2): convierte el estado continuo en eventos discretos. `stream_planner.hpp`/`stream_intent.hpp` son el equivalente del planner para **streaming** (declarar un stream, resolver su vía y avisar).

Volver al [índice de `audio/`](README.md) · [Referencia](../README.md) · [manual](../../README.md).
