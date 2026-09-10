# Demo 062: game audio — capa de audio de juego (GameAudio)

Demuestra `eng::audio::GameAudio`: el juego registra sonidos en un `SampleBank`
por `id` y los dispara con `play(id)` (cooldown, límite de instancias,
prioridad), mientras suena música Protracker con **ducking** (la música baja
cuando suena la alarma).

Qué suena: música en bucle (AUD1) + alarma en bucle con `duck_music` (mixer,
AUD0). FIRE dispara un "beep" con cooldown de 4 frames.

```bash
tools/build/build-demo.sh demos/amiga/062_game_audio --clean
tools/run/run-demo.sh       demos/amiga/062_game_audio
```

La política pura (`SampleBank`/`allow_trigger`) está cubierta por HOST-008. Ver
`docs/engine/architecture/GAME_AUDIO.md` para la guía completa (API + generación
de música y sonidos).
