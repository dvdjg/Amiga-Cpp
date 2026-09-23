# 276_music_mixer — música real (3 canales HW) + mixer de SFX (4 voces SW)

Junta los dos subsistemas del modo `Game` de `eng::audio` en una sola demo:

- **Música**: un módulo real por `P61Player` (`.p61`) o `PtPlayer` (`.mod`), silenciando AUD0 con
  `set_music_channel_mask(0x0E)` → la música suena a **3 canales HW** (AUD1‑AUD3).
- **SFX**: 4 samples de percusión reales de `st-xx` (bombo/caja/hihat/palmas) por **1 canal HW**
  con el mixer de Photon (**4 voces SW**, `MixCh0..3`), patrón de 16 pasos.

## Módulos (`-DMED_MOD=<n>`, por defecto 0)

| n | Módulo | Tamaño | Formato | Contador mixer | Golpes |
|---|---|---|---|---|---|
| **0** | `testmod.p61` (defecto) | 5 KB | P61 | ~119 (~60 Hz) | 18 |
| 1 | `SneakyChick.mod` | 87 KB | ProTracker | ~119 | 18 |
| 2 | `jazzcat-boogie_town.mod` | 241 KB | ProTracker | **~16** | **2** |

## Hallazgos

- **`testmod.p61` suena** a 3 canales (P61). Sus samples van **empaquetados** (bit 6 del `byte 3`),
  así que `P61_Init` **exige un buffer** de descompresión (tamaño en el `offset 4`): la demo lo aloca
  en Chip y lo pasa (`AudioSystem::play_music(module, format, buffer)`). Ver
  `p61_needs_sample_buffer`/`p61_sample_buffer_size` en `music_player.hpp`.
- **Un módulo de 241 KB estrangula el mixer** (~8 Hz en vez de ~49): la carga/reloc del módulo deja al
  mixer casi sin servicio (los SFX apenas suenan). No es del reproductor sino del **coste del módulo
  grande**; el de 5–87 KB va a ~60 Hz.

## Evidencia (`mark_ready` a frame 120)

`detail` = golpes disparados; `g_eng_run_status.frame` = `counter<<24 | dmaconr<<16 | kick<<8 | snare`.

## Build/run

```bash
bash tools/build/build-demo.sh demos/amiga/276_music_mixer --debug
WINUAE_SIDE_CHANNEL_PORT=2421 bash tools/run/run-demo.sh demos/amiga/276_music_mixer --wait-port 300
# Otro módulo: EXTRA_DEFINES="-DMED_MOD=2" bash tools/build/build-demo.sh ...
```

Requiere `out/assets/audio/{kick,snare,hihat,claps}_mix.raw` (de `st-xx`, ver `tools/audio/README.md`).
