# MusicPlayer — reproductores de música

El `MusicPlayer` de `ENGINE_DESIGN.md` §2.6 envuelve los reproductores de música
asm del demoscene-repo como backends de la intención `eng::audio::MusicEvent`
(música de tracker). Aquí se fija el estado y el plan.

## Estado (2026-09)

| Formato | Fuente | Estado |
|---|---|---|
| P61 | `lib/libp61/p61.asm` + `P6112-Play.i` | **Importado**: `support/music/p61.asm`; envoltura `eng::audio::P61Player` en `music_player.hpp`. Demo `059` lo enlaza. |
| Protracker (MOD) | `lib/libpt/pt.asm` + `ptplayer.i` (95 KB) | **Importado**: `support/music/pt.asm` (+ `ptplayer.i`, `vbr.s` con `_ExcVecBase=0`). Ensambla y enlaza. **Pendiente**: envoltura + demo. |
| OctaMED (8 canales SW) | `KONEY/octamed_playroutines_amiga` (`support/music/octamed/` + `support/music/med.asm`) | **Infra lista (A1)**. VASM (MOT) ensambla/enlaza el playroutine + el módulo `INCBIN` (`ChipData.MEMF_CHIP`), 0 símbolos externos (salvo `_chipzero`, aportado por el wrapper), y exporta `_startmusic`/`_endmusic`. Envoltura `eng::audio::OctaMedPlayer` + `MusicFormat::OctaMED` (opt-in con `-DENG_AUDIO_OCTAMED`, para no arrastrar el módulo en otras demos). **Runtime abierto**: `jsr _startmusic` cuelga bajo el engine → ver `docs/debugging/investigaciones/octamed-startmusic-hang.md`. |
| AHX | `lib/libahx/ahx.asm` + `AHX-Replayer000.BIN` | **Pendiente**: necesita el blob `.BIN` y la libc del demoscene-repo (`MemAlloc`/`OpenFile`/`FileRead`/`FileClose`). |

## API de los reproductores

- **P61** (`p61.h`): `P61_Init(module,samples,buffer)` / `P61_Music` (por frame) /
  `P61_End` / `P61_SetPosition`; `P61_ControlBlock` (volumen `Master`, flag `Play`,
  posición `Pos`). Es frame-driven (llamar `P61_Music` una vez por frame).
- **PTPlayer** (`ptplayer.h`): `mt_init`/`mt_music`/`mt_end` (frame-driven) y
  `mt_install`/`mt_remove` (opcional, por CIA). Para coexistir con el mixer usa
  `mt_EnableChannelMask` (Frank Wille, dominio público). El wrapper `pt.asm` del
  demoscene-repo expone `PtInit`/`PtEnd`/`PtInstallCIA`/`PtRemoveCIA`/`PtEnable`.
- **OctaMED** (KONEY): `Init`/`Play`/`Stop` sobre un módulo MED; mezcla **8 voces software** en los
  4 canales HW (ocupa toda Paula). Se usa en el modo `TitleOctaMED`
  ([GAME_AUDIO.md](GAME_AUDIO.md) §7); al salir del título se para y se vuelve al modo `Game`.

## Pasos pendientes

1. Envoltura `eng::audio::PtPlayer` (frame-driven: `mt_init` + `mt_music` por
   frame, `mt_end`), siguiendo el patrón de `P61Player` (punteros solo en la capa
   interna, `MusicModule` = `Span<const u8>`).
2. Demo con un módulo incrustado (`.p61`/`.mod` en `assets/amiga/audio/`),
   reservando el canal del mixer: el mixer usa `DMAF_AUD0`; la música usa
   `AUD1..AUD3`, y el módulo no toca el canal del mixer.
3. AHX: incbin del `AHX-Replayer000.BIN` + shims de `MemAlloc`/`MemFree`/`OpenFile`/
   `FileRead`/`FileClose`.
4. OctaMED (KONEY): importar la playroutine de 8 voces SW, envolverla (`eng::audio::OctaMedPlayer`)
   y enlazarla con el modo `TitleOctaMED`; demo de pantalla de título.

## Convenio mixer + música

El Audio Mixer y el reproductor conviven reservando canales de Paula distintos:
el mixer en `AUD0` (config actual), la música en el resto. Para ptplayer, usar
`mt_EnableChannelMask` para excluir el canal del mixer. Ver
`AUDIO_MIXER.md` y la sección «Combining the mixer and a music player» de la
documentación del mixer.

## Orden de arranque

Arrancar la música primero y el mixer después (muchos reproductores inicializan
todos los canales al arrancar, incluso los vacíos).

## Límite medido: el tamaño del módulo ralentiza al mixer (demo 276)

Hallazgo (medido, no resuelto del todo): con música **+** mixer a la vez, el **contador de la IRQ de
audio del mixer** avanza a ritmo **constante pero mucho menor** cuanto **más grande** es el módulo. En
`demos/amiga/276_music_mixer` (música en AUD1‑3 por P61/PtPlayer + mixer en AUD0, a frame 120):

| Módulo | Tamaño | Contador del mixer | Golpes |
|---|---|---|---|
| `testmod.p61` (P61) | 5 KB | ~119 (~60 Hz) | 18 |
| `SneakyChick.mod` (ProTracker) | 87 KB | ~119 | 18 |
| `jazzcat-boogie_town.mod` (ProTracker) | 241 KB | **~16 (~8 Hz)** | 2 |

**Acotado por experimentos:**

- **Es ritmo, no arranque tardío**: a frame 480 el módulo grande da ~59 (~proporcional a 120→480); el
  mixer avanza lento desde el principio.
- **No es el reproductor**: P61 y ProTracker dan 119 con módulos pequeños → el tipo de player no importa.
- **No es la RAM del módulo**: mover el módulo de Chip a `MEMF_ANY` no cambia el contador (descartada
  la contención de bus por el hunk en Chip).

**Hipótesis abierta**: con módulos grandes, el reproductor (que corre en **CIA**, nivel 2) **se come la
IRQ de audio del mixer** (nivel 4) o **reescribe registros de audio más a menudo**; el mixer pierde
IRQs. Para reproducirlo: `demos/amiga/276_music_mixer` con `-DMED_MOD=2` (y `-DK_REPORT_FRAME=N`). Un
módulo **moderado** (≤ ~100 KB) va fino.

**No hay límite documentado del tamaño del módulo** en el reproductor (P61/PtPlayer) ni en el mixer.
El mixer solo documenta límites de **sus buffers internos** (`mixer_buffer_size`,
`mixer_plugin_buffer_size` en `support/audio_mixer/mixer.i`), que son **pequeños y fijos** (fracción
de segundo × nº de voces) y **no crecen con el módulo**: el mixer no "carga" el módulo.

La vía correcta para audio grande **no** es incrustarlo entero (es lo que agrava el problema medido),
sino **streaming desde disco con footprint pequeño**, ya diseñado en
[`AUDIO_STREAMING.md`](AUDIO_STREAMING.md) (`PcmStream`/`ChunkStream`, chunks de 4–8 KB, roadmap A5):
mientras Paula reproduce un buffer, el disco llena el siguiente. Para un reproductor de módulos, la
analogía es cargar el módulo (o sus samples) por partes, no `INCBIN`ar 200+ KB en el ejecutable.

## Protocolo P61 (`testmod.p61`) y samples empaquetados

Un `.p61` puede llevar los **samples empaquetados** (bit 6 del `byte 3`, tras el signo opcional `P61A`);
entonces `P61_Init` **exige un buffer** de descompresión cuyo tamaño está en el `offset 4` del módulo.
Se detecta con `p61_needs_sample_buffer` / `p61_sample_buffer_size` (`music_player.hpp`) y se pasa a
`AudioSystem::play_music(module, format, buffer)`. El `.p61` oficial es la playroutine
`support/music/p61/P6112-Play.i` (P6112 de Photon/Scoopex), integrada por `support/music/p61.asm`.

## Referencias

- Cabeceras: `demoscene-repo-orig/include/p61.h`, `ptplayer.h`, `ahx.h`.
- Ejemplos: `effects/playp61/playp61.c`, `effects/playahx/playahx.c`.
- Tutoriales: `docs/tutoriales/46-playp61.md`, `47-playprotracker.md`, `44-playahx.md`.
- Reproductores externos: P61 (`cahirwpz/demoscene`, `effects/playp61/playp61.c`), OctaMED
  (`KONEY/octamed_playroutines_amiga`), Protracker (`Frank Wille`, `ptplayer`).
