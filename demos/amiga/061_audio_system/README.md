# Demo 061: audio system — fachada unificada SFX + música

Consolida la API de audio: `eng::audio::AudioSystem` es el único punto de entrada
para efectos (Audio Mixer 3.7) y música (ptplayer/P61). El juego llama a
`play_sfx`/`play_music` sin conocer los backends.

Qué hace: reproduce un módulo Protracker (generado en memoria, nota C-2 en el
canal 1/AUD1) y, a la vez, efectos por el mixer en AUD0 (alarma en bucle + beep
por FIRE). La coexistencia usa canales de Paula separados.

## Orden canónico (importante)

1. Arrancar la música (`play_music`) — los reproductores inicializan todos los
   canales de audio al arrancar.
2. Reservar el canal del mixer (`set_music_channel_mask(1)` → AUD0 libre).
3. Arrancar el mixer (`init`).

## Verificación

`mark_ready` se emite cuando el DMA de SFX y música están activos; el detalle
guarda `DMACONR` (debe mostrar `AUD0EN` y `AUD1EN` activos).

```bash
tools/build/build-demo.sh demos/amiga/061_audio_system --clean
tools/run/run-demo.sh       demos/amiga/061_audio_system
```

Ver `docs/engine/architecture/AUDIO_MIXER.md` y `MUSIC_PLAYER.md`.
