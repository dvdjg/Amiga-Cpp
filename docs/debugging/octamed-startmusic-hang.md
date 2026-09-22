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
- **`a6`**: el playroutine usa direcciones **absolutas** `$dffxxx` (p. ej. `$dff096`), no un base en
  `a6` (0 referencias a `(a6)`) → no es el wrapper de Photon.

## Hipótesis

`_startmusic` (→ `_RelocModule` → `_InitPlayer` → `_PlayModule`) **espera algo de timing/VBlank**:
hay un bucle que sondea `$dff007` (byte alto de `VHPOSR`) en `MED_PlayRoutine.i:2160-2162`, y la
inicialización instala su propio ISR de VBlank/CIA. Puede chocar con el `wait_vblank` **por polling**
del engine (que no consume la misma vía) o quedarse esperando una condición que el modo takeover no
produce.

## Siguiente paso

1. Depurar con GDB: breakpoint en `_startmusic`, *single-step* y leer PC/registros cuando se cuelga
   (¿`_RelocModule`, `_InitPlayer` o el bucle de `$dff007`?).
2. Comparar con el ejemplo de KONEY (`OCTAMED_example3.s` + `PhotonsMiniWrapper1.04.s`), que fija el
   custom base y su propio arranque, para aislar qué condición de entorno exige el playroutine.
3. Revisar `_InitPlayer` (VBlank/CIA) frente al bucle del engine (`docs/engine/architecture/ENGINE_*`).

Referencias: `docs/engine/architecture/MUSIC_PLAYER.md` (fila OctaMED),
`docs/guides/roadmap/ROADMAP_AUDIO.md` (A1), `../octamed_playroutines_amiga/`.
