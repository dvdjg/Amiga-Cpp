# Referencia — `audio/`

Audio y música del engine (`eng::audio`). Paula solo tiene **4 canales DMA**, así que el módulo separa: la **intención** (`SampleEvent`/`MusicEvent`), el **reparto de voces** (`AudioMixer` → `AudioPlan`), la **música** de tracker (reproductores asm: P61/Pt/OctaMED) y el **streaming** desde disco. Ver `docs/engine/architecture/GAME_AUDIO.md`.

```
   intención (juego)          reparto (puro, host-test)           hardware
   ─────────────────          ──────────────────────────          ────────
   SampleEvent/MusicEvent ──► AudioMixer → AudioPlan ──► backend escribe AUDx* ──► Paula
   SoundPlanner.declare()     (asigna 4 canales, presupuesto)     feeder por IRQ (nivel 4)
```

## Páginas

| Página | Qué documenta |
|---|---|
| [`mixer.md`](mixer.md) | `AudioMixer`/`AudioPlan`/`SampleEvent`/`MusicEvent`/presupuesto, `AudioMode`/canales, `SfxMixer` (mixer Photon por software). |
| [`music.md`](music.md) | `AudioSystem`/`detect_music_format`, `P61Player`/`PtPlayer`/`OctaMedPlayer`/`MusicModule`, codecs (`pcm_codec`, `media`), `PcmStream`/`MediaStreamBackend`, `acp1`/`aplib`/`fib_delta`/`ima_adpcm`/`auzx`. |
| [`planner.md`](planner.md) | `SoundPlanner`/`SoundQueue`/`MixerExecutor`, `audio_events`, `stream_planner`/`stream_intent`. |

## Reglas

- **Paula = 4 canales DMA**: `AudioConfig`/`AudioMode` (`audio_mode.hpp`) reparten las voces entre SFX y música; el plan las asigna.
- El reparto de voces es **puro** (`AudioPlan`) y host-testable: el backend solo escribe registros.
- El **feeder por IRQ de Paula** (nivel 4) avanza el DMA; el reparto del frame lo gobierna el `SoundPlanner` (el bucle, **no** la ISR).
- Los reproductores de tracker son **asm de la demoscene** (`support/music/`, `support/audio_mixer/`) envueltos en C++23; los punteros de registro quedan en la capa interna.

Volver a [Referencia](../README.md) · [índice del manual](../../README.md).
