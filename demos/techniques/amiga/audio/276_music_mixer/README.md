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

## Cargar el módulo desde disco (`-DMED_FROM_DISK="data/audio/<mod>"`)

En vez de incrustar el módulo (`INCBIN`, lo mete en el ejecutable), se carga **desde disco** con
`file_open`/`file_read_sync` a un buffer Chip. **Debe hacerse ANTES de `takeover_display`**: `dos.library`
necesita interrupciones/`DoIO`, que el takeover apaga (si no, cuelga). El fichero lo publica
`tools/fs/make-volume.mjs` en `data/audio/`.

```bash
EXTRA_DEFINES='-DMED_FROM_DISK="data/audio/SneakyChick.mod"' bash tools/build/build-demo.sh demos/techniques/amiga/audio/276_music_mixer --debug
CFG=$(ls -dt out/demos/276_music_mixer/A500_med_from_* | head -1 | xargs basename)
node tools/fs/make-volume.mjs --out "out/run/276_music_mixer/$CFG/dh1"
WINUAE_GDB_PORT=2355 WINUAE_SIDE_CHANNEL_PORT=2421 bash tools/run/run-demo.sh demos/techniques/amiga/audio/276_music_mixer --config "$CFG"
```

## Hallazgos

- **`testmod.p61` suena** a 3 canales (P61). Sus samples van **empaquetados** (bit 6 del `byte 3`),
  así que `P61_Init` **exige un buffer** de descompresión (tamaño en el `offset 4`): la demo lo aloca
  en Chip y lo pasa (`AudioSystem::play_music(module, format, buffer)`). Ver
  `p61_needs_sample_buffer`/`p61_sample_buffer_size` en `music_player.hpp`.
- **Un módulo de 241 KB estrangula el mixer** (~8 Hz en vez de ~49): deja al mixer casi sin servicio
  (los SFX apenas suenan). El de 5–87 KB va a ~60 Hz.
- **Cargarlo de disco NO arregla eso**: medido, el módulo grande da el **mismo** contador (~16) tanto
  incrustado como cargado de disco → el límite **no es "incrustar"** ni el tamaño del ejecutable, sino
  el **módulo grande en sí** (RAM/CPU del reproductor). Cargar de disco **sí** evita que el `.exe`
  crezca; para el límite, usar un módulo **moderado** (≤ ~100 KB) o **streaming** (`AUDIO_STREAMING.md`).

## Evidencia (`mark_ready` a frame 120)

`detail` = golpes disparados; `g_eng_run_status.frame` = `counter<<24 | dmaconr<<16 | kick<<8 | snare`.

## Build/run

```bash
bash tools/build/build-demo.sh demos/techniques/amiga/audio/276_music_mixer --debug
WINUAE_SIDE_CHANNEL_PORT=2421 bash tools/run/run-demo.sh demos/techniques/amiga/audio/276_music_mixer --wait-port 300
# Otro módulo: EXTRA_DEFINES="-DMED_MOD=2" bash tools/build/build-demo.sh ...
```

Requiere `out/assets/audio/{kick,snare,hihat,claps}_mix.raw` (de `st-xx`, ver `tools/audio/README.md`).
