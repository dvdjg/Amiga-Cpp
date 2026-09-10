# MusicPlayer — reproductores de música (plan de integración)

El `MusicPlayer` de `ENGINE_DESIGN.md` §2.6 envuelve los reproductores de música
asm del demoscene-repo como backends de la intención `eng::audio::MusicEvent`
(música de tracker). Aquí se fija el plan y el estado.

## Reproductores disponibles (`demoscene-repo-orig`)

| Formato | Fuente | API | Notas |
|---|---|---|---|
| Protracker (MOD) | `lib/libpt/pt.asm` + `ptplayer.asm` (95 KB) | `mt_init`/`mt_music`/`mt_end` (`mt_EnableChannelMask` para reservar canales) | Frank Wille ptplayer 6.x; compat con el Audio Mixer (canal por máscara) |
| P61 | `lib/libp61/p61.asm` + `P6112-Play.i` (88 KB) | `P61_Init`/`P61_Music`/`P61_End`/`P61_SetPosition`/`P61_MasterVolume` | Jazzcat; un solo `.asm` |
| AHX | `lib/libahx/ahx.asm` + `AHX-Replayer000.BIN` | init/play | Necesita un blob binario precompilado (`.BIN`) |

Cabeceras C: `include/p61.h`, `include/ptplayer.h`, `include/ahx.h`. Ejemplos de
uso: `effects/playp61/playp61.c`, `effects/playahx/playahx.c`. Tutoriales:
`docs/tutoriales/46-playp61.md`, `47-playprotracker.md`, `44-playahx.md`.

## Pasos de integración (pendiente)

1. Copiar el `.asm` (+ `.i`/`.BIN`) del reproductor elegido a `support/music/`.
2. Ensamblar con VASM a ELF (mismo mecanismo que `support/audio_mixer/` en
   `tools/build/build-demo.sh`).
3. Crear `eng/audio/music_player.hpp`: envoltura C++23 con
   `play(MusicModule)`/`stop()`/`update()` (por frame, en VBlank)/`set_volume()`.
   `MusicEvent` (ya en `eng/audio/audio.hpp`) alimenta esta capa.
4. Demo con un módulo incrustado (`.mod`/`.p61`), reservando el canal del mixer:
   el mixer usa `DMAF_AUD0`; la música usa `AUD1..AUD3` (o viceversa), y el
   módulo no debe tocar el canal del mixer.

## Convenio mixer + música

El Audio Mixer y el reproductor conviven reservando canales de Paula distintos:
el mixer en `AUD0` (config actual), la música en el resto. Para ptplayer, usar
`mt_EnableChannelMask` para excluir el canal del mixer. Ver
`AUDIO_MIXER.md` y la sección «Combining the mixer and a music player» de la
documentación del mixer.

## Orden de arranque

Arrancar la música primero y el mixer después (muchos reproductores inicializan
todos los canales al arrancar, incluso los vacíos).
