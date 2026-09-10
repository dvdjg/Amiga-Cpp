# Demo 059: music player — SFX (Audio Mixer) + música (P61) conviviendo

Demuestra la integración del `MusicPlayer` (reproductor **P61** de
Photon/Scoopex, `support/music/p61.asm`) junto al SFX mixer. La capa de juego usa
`eng::audio::SfxMixer` y `eng::audio::P61Player`, sin punteros crudos (la
memoria se expresa con `Span<const u8>`).

Qué suena: una "alarma" en bucle por el SFX mixer (canal `AUD0`). El reproductor
P61 queda enlazado y listo.

## Cómo añadir música real

1. Incrusta un módulo `.p61` (p. ej. desde `demoscene-repo-orig/effects/playp61/data/`).
2. Pásalo como `MusicModule { Span<const u8>(module, len) }` a `music.play(...)`.
3. El mixer usa `AUD0`; configura el reproductor P61 (`channels`) para que no
   use el canal del mixer si quieres SFX + música a la vez.

```bash
tools/build/build-demo.sh demos/amiga/059_music_player --clean
tools/run/run-demo.sh       demos/amiga/059_music_player
```

Ver `docs/engine/architecture/MUSIC_PLAYER.md` y `AUDIO_MIXER.md`.
