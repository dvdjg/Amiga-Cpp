# HOST-370 · audio_plan (presupuesto de AudioPlan)

Test host del **presupuesto de audio** (`eng/audio/audio.hpp`): `AudioBudget`,
`AudioBudgetLimits` y `AudioBudgetReport`, el análogo de `BlitBudget` para el
`FramePlan`. El `AudioMixer` lo actualiza al repartir canales y el plan lo compara
contra sus límites (`Ok`/`Warning`/`Exceeded`). Lógica pura, sin hardware.

## Qué valida

- El plan por defecto tiene presupuesto cero e informe `Ok`.
- `play()` acumula voces (`voices`) y palabras (`words`) al asignar canales.
- Superar `max_voices` marca `voices_exceeded` y estado `Exceeded`.
- Estar entre `warning_voices` y `max_voices` marca `Warning` (sin exceder).
- El límite de `words` (`max_words`/`warning_words`) se evalúa igual.
- `begin_frame()` reinicia canales y presupuesto, pero conserva los límites.

## Relación con el planner

`AudioPlan` es el **contrato** que unifica el audio con el planner
(`INTENT_PLANNER.md` §6: «el audio es un plan análogo»): reparto de voces
(SFX/música) + presupuesto por frame, con la IRQ de Paula como **feeder**. El
backend materializa los canales en registros Paula (`AUDxLCH/LCL/LEN/PER/VOL`).
