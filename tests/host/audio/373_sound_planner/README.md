# HOST-373 · sound_planner (intención + plan + `Msg`)

Test host de `eng/audio/sound_planner.hpp`: `SoundPlanner` une las tres piezas del
planner de audio en un objeto — la **cola** de intención (`SoundQueue`, mecanismo en
`eng/core/util`), el **plan** (`AudioMixer` → `AudioPlan`) y el **aviso** (`eng/os`,
`Msg IntentDone`/`AudioUnderrun`).

## Qué valida

- `declare()` encola sin bloquear ni ejecutar (`plan` vacío, puerto vacío hasta `flush`).
- `flush()` drena al `AudioPlan` y **postea un `Msg IntentDone` por petición** (con su ticket).
- `notify_underrun()` + `flush()` postea `AudioUnderrun` **una sola vez por evento** (flanco).

## Por qué

Antes cada pieza existía por separado (`SoundQueue`, `AudioMixer`, `IntentDonePoster`,
`AudioMsgEdges`). `SoundPlanner` las conecta con una sola llamada por frame desde el
bucle (la ISR solo alimenta el `AudioFeeder`). Ver `INTENT_PLANNER.md` §6.
