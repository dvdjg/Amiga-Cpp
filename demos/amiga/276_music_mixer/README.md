# 276_music_mixer — música real (3 canales HW) + mixer de SFX (4 voces SW)

Junta los dos subsistemas del modo `Game` de `eng::audio` en una sola demo:

- **Música**: un `.mod` ProTracker **real** por `PtPlayer` (timing CIA), silenciando AUD0 con
  `set_music_channel_mask(0x0E)` → la música suena a **3 canales HW** (AUD1‑AUD3).
- **SFX**: 4 samples de percusión reales de `st-xx` (bombo/caja/hihat/palmas) por **1 canal HW**
  con el mixer de Photon (**4 voces SW**, `MixCh0..3`), patrón de 16 pasos.

## Hallazgo: el tamaño del módulo estrangula el mixer

Medido a frame 120 (`frame` = contador del mixer / DMACONR / kick / snare):

| Módulo | Tamaño | Contador mixer | Golpes |
|---|---|---|---|
| `SneakyChick.mod` (defecto) | 87 KB | ~119 (~60 Hz) | 18 |
| `jazzcat-boogie_town.mod` | 241 KB | ~16 (~8 Hz) | 2 |

Con el módulo de **241 KB** el **contador del mixer apenas avanza** (~8 Hz en vez de ~49): la música
(módulo grande + reloc + presión de memoria) deja al mixer casi sin servicio → los SFX apenas suenan.
Con el de **87 KB** va a ~60 Hz y el patrón de batería suena. **Por eso el defecto es el pequeño.**

## Evidencia (`mark_ready` a frame 120)

`detail` = golpes disparados; `g_eng_run_status.frame` = `counter<<24 | dmaconr<<16 | kick<<8 | snare`.

## Build/run

```bash
bash tools/build/build-demo.sh demos/amiga/276_music_mixer --debug
WINUAE_SIDE_CHANNEL_PORT=2421 bash tools/run/run-demo.sh demos/amiga/276_music_mixer --wait-port 300

# Módulo grande (para reproducir el estrangulamiento):
EXTRA_DEFINES="-DMED_BIG=1" bash tools/build/build-demo.sh demos/amiga/276_music_mixer --debug
```

Requiere `out/assets/audio/{kick,snare,hihat,claps}_mix.raw` (de `st-xx`, ver `tools/audio/README.md`).
