# OctaMED: `_startmusic` cuelga bajo el engine (A1, abierto)

**Estado**: abierto. La infraestructura de A1 está lista y validada; el arranque en runtime
(`_startmusic`) **cuelga/crashea** bajo el engine y queda pendiente de depurar.

## Síntoma

Una demo que llama a `eng::audio::OctaMedPlayer::play()` (emite `jsr _startmusic`) **no alcanza
READY**: el runner reporta `side_channel_unavailable` (la CPU se cuelga antes de procesar frames).
La **misma demo sin `_startmusic` alcanza READY** (`state=3`) → el fallo está en `_startmusic`.

## Qué está validado (spike)

- VASM (MOT) ensambla `support/music/med.asm` = `octamed/med_feature_control.i` +
  `octamed/MED_PlayRoutine.i` + el módulo `INCBIN` en la sección **`ChipData.MEMF_CHIP`**
  (`RC=0`).
- **0 símbolos indefinidos** (el único externo era `_chipzero`, un `DC.L 0` de silencio que aporta
  el wrapper).
- `_startmusic`/`_endmusic` globales y enlazados (aparecen en el `.map`, junto con `_chipzero` y
  `ChipData.MEMF_CHIP`): el módulo **no** lo descarta `--gc-sections`.
- El módulo `octamed_test.med` es **MMD1** (magic `MMD1`), soportado con `PLAYMMD0=0`.

## Qué se probó (sin éxito)

- **Timing**: `VBLANK=1,CIAB=0` (ISR de VBlank del playroutine) y `VBLANK=0,CIAB=1` (CIA-B) — ambos
  cuelgan.
- **Sin mixer**: `OctaMedPlayer` directo (sin `AudioSystem`/mixer de SFX) — sigue.
- **Orden**: arrancar `_startmusic` **antes** de `takeover_display` — sigue colgando → no es el
  takeover el que rompe el entorno de `exec`.
- **`a6`**: el playroutine usa direcciones **absolutas** `$dffxxx` (p. ej. `$dff096`) y su propio
  registro de datos `A4 = DB`; no espera un base en `a6` del llamador (`_AudioInit` guarda y
  re-setea `A6`).

## Hipótesis

`_startmusic` → `_RelocModule` → `_InitPlayer` → `_PlayModule`. El sospechoso principal está en
**`_InitPlayer` → `_AudioInit`** (`MED_PlayRoutine.i:2520`): usa **`A4 = DB`** como base de datos y
**`A6 = SysBase`** (`MOVEA.L 4.W,A6`) para llamar a **exec**:

- `AllocSignal` / `FindTask` / `OpenDevice` / `OpenResource` (`4.W`),
- `AddICRVector` (CIA) y **`AddIntServer`** (`JSR -$a8`).

Es decir, el playroutine **necesita el sistema de interrupciones de exec** para instalar su
VBlank/CIA. En un demo *takeover* como el nuestro, si el takeover deja `exec`/las interrupciones en
un estado no funcional, `AddIntServer`/`OpenResource` pueden **colgarse**. Esto encaja con que el
`side_channel_unavailable` (no un READY tardío) y con que el `a6` del llamador no sea la causa
(`_AudioInit` guarda y re-setea `A6`; el playroutine usa `A4`/`A6` internamente).

## Siguiente paso

1. Depurar con GDB: breakpoint en `_startmusic`, *single-step* y leer PC/registros cuando se cuelga
   (¿`_RelocModule`, `_InitPlayer` o el bucle de `$dff007`?).
2. Comparar con el ejemplo de KONEY (`OCTAMED_example3.s` + `PhotonsMiniWrapper1.04.s`), que fija el
   custom base y su propio arranque, para aislar qué condición de entorno exige el playroutine.
3. Revisar `_InitPlayer` (VBlank/CIA) frente al bucle del engine (`docs/engine/architecture/ENGINE_*`).

Referencias: `docs/engine/architecture/MUSIC_PLAYER.md` (fila OctaMED),
`docs/guides/roadmap/ROADMAP_AUDIO.md` (A1), `../octamed_playroutines_amiga/`.
