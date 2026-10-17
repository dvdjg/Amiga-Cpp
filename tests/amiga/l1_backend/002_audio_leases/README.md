# L1-002 - audio leases (on-target)

Canario de **audio en target**: ejercita el `AudioSystem` con el **mixer ASM real** (no corre en host) y comprueba el contrato de canales sobre Paula.

## Qué valida

1. `init(GameSfxOnly)` arranca el mixer y expone 4 canales software (`total_sfx_channels() == 4`).
2. Cuatro `play_sfx_on` (MixCh0..MixCh3, prioridad 5, loop) ocupan los cuatro canales.
3. Reproducir en un canal ocupado con **prioridad menor** se rechaza (`-1`): la voz no se roba.
4. Tras `stop_sfx`, reocupar el canal con **prioridad mayor** vuelve a sonar (el mixer aplica el stop en el tick de su IRQ; el canario espera un VBlank antes de reocupar).
5. Con SFX sonando, `DMACONR` tiene **AUD0EN** (el mixer es `MIXER_SINGLE`: un canal hardware, `support/audio_mixer/mixer_config.i`); tras `shutdown` no queda ningún canal de audio DMA activo.

## Evidencia

`g_eng_run_status.detail` = 6 bits de checks (todos a 1) y el `DMACONR` leído con SFX activo en la mitad alta. El runner lo lee por canal lateral y `verify.sh` lo comprueba.

## Ejecución

```bash
bash tests/amiga/l1_backend/002_audio_leases/verify.sh
```

Referencias: `docs/engine/architecture/GAME_AUDIO.md` §7-8, `ROADMAP_AUDIO.md` (A0), HOST-269 (reparto de canales puro) y `ROADMAP_API_COHERENCE.md` §F8 (canarios de audio en target).
