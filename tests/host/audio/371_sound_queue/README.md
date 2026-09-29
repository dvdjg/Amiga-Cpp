# HOST-371 · sound_queue (intención de sonido → `AudioPlan`)

Test host de `eng/audio/sound_queue.hpp`: el **vocabulario de sonido** del planner
sobre el **mecanismo** genérico `eng::IntentQueue` (`eng/core/util/intent_queue.hpp`).

## Qué valida

- `sample_event_of` mapea todos los campos de `SoundIntent` a `SampleEvent`.
- Declarar una intención **no** ejecuta ni bloquea (`mixer.active_count() == 0`).
- `flush()` vuelca las intenciones al `AudioPlan` vía `MixerExecutor` y avisa por `Done`.
- `voice` (voz preferida) se respeta en el plan; las demás voces quedan libres.

## Relación con el planner

El audio es un **plan análogo** al de blit (`INTENT_PLANNER.md` §6): `SoundIntent` →
`SoundQueue` (mecanismo compartido) → `MixerExecutor` → `AudioPlan`. La cola es la **misma**
plantilla `eng::IntentQueue` que usa el blit y el dibujo, así que `eng/audio` **no** depende de
`eng/graphics`.
