# MusicPlayer — reproductores de música

El `MusicPlayer` de `ENGINE_DESIGN.md` §2.6 envuelve los reproductores de música
asm del demoscene-repo como backends de la intención `eng::audio::MusicEvent`
(música de tracker). Aquí se fija el estado y el plan.

## Estado (2026-09)

| Formato | Fuente | Estado |
|---|---|---|
| P61 | `lib/libp61/p61.asm` + `P6112-Play.i` | **Importado**: `support/music/p61.asm`; envoltura `eng::audio::P61Player` en `music_player.hpp`. Demo `059` lo enlaza. |
| Protracker (MOD) | `lib/libpt/pt.asm` + `ptplayer.i` (95 KB) | **Importado**: `support/music/pt.asm` (+ `ptplayer.i`, `vbr.s` con `_ExcVecBase=0`). Ensambla y enlaza. **Pendiente**: envoltura + demo. |
| AHX | `lib/libahx/ahx.asm` + `AHX-Replayer000.BIN` | **Pendiente**: necesita el blob `.BIN` y la libc del demoscene-repo (`MemAlloc`/`OpenFile`/`FileRead`/`FileClose`). |

## API de los reproductores

- **P61** (`p61.h`): `P61_Init(module,samples,buffer)` / `P61_Music` (por frame) /
  `P61_End` / `P61_SetPosition`; `P61_ControlBlock` (volumen `Master`, flag `Play`,
  posición `Pos`). Es frame-driven (llamar `P61_Music` una vez por frame).
- **PTPlayer** (`ptplayer.h`): `mt_init`/`mt_music`/`mt_end` (frame-driven) y
  `mt_install`/`mt_remove` (opcional, por CIA). Para coexistir con el mixer usa
  `mt_EnableChannelMask` (Frank Wille, dominio público). El wrapper `pt.asm` del
  demoscene-repo expone `PtInit`/`PtEnd`/`PtInstallCIA`/`PtRemoveCIA`/`PtEnable`.

## Pasos pendientes

1. Envoltura `eng::audio::PtPlayer` (frame-driven: `mt_init` + `mt_music` por
   frame, `mt_end`), siguiendo el patrón de `P61Player` (punteros solo en la capa
   interna, `MusicModule` = `Span<const u8>`).
2. Demo con un módulo incrustado (`.p61`/`.mod` en `assets/amiga/audio/`),
   reservando el canal del mixer: el mixer usa `DMAF_AUD0`; la música usa
   `AUD1..AUD3`, y el módulo no toca el canal del mixer.
3. AHX: incbin del `AHX-Replayer000.BIN` + shims de `MemAlloc`/`MemFree`/`OpenFile`/
   `FileRead`/`FileClose`.

## Convenio mixer + música

El Audio Mixer y el reproductor conviven reservando canales de Paula distintos:
el mixer en `AUD0` (config actual), la música en el resto. Para ptplayer, usar
`mt_EnableChannelMask` para excluir el canal del mixer. Ver
`AUDIO_MIXER.md` y la sección «Combining the mixer and a music player» de la
documentación del mixer.

## Orden de arranque

Arrancar la música primero y el mixer después (muchos reproductores inicializan
todos los canales al arrancar, incluso los vacíos).

## Referencias

- Cabeceras: `demoscene-repo-orig/include/p61.h`, `ptplayer.h`, `ahx.h`.
- Ejemplos: `effects/playp61/playp61.c`, `effects/playahx/playahx.c`.
- Tutoriales: `docs/tutoriales/46-playp61.md`, `47-playprotracker.md`, `44-playahx.md`.
