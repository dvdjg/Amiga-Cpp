# Referencia — mixer y modos

El **reparto de voces** de Paula: la intención (eventos) se compila a un `AudioPlan` sobre los 4 canales DMA. Es **puro** (solo `eng::core`), host-testable, sin heap ni RTTI.

## `audio.hpp`

| Tipo | Qué es |
|---|---|
| `SampleEvent` (`audio.hpp:21`) | Efecto: muestra 8-bit (Chip) + `length_words`/`period`/`volume`/`channel_hint` (`0xff` = auto). |
| `MusicEvent` (`audio.hpp:30`) | Música: `module` + `play`/`stop`. |
| `AudioBudget` (`audio.hpp:39`) | Voces DMA ocupadas y palabras que leerá el DMA: para `AudioPlan` lo que `BlitBudget` a `FramePlan` (estimación verificable, no ciclos exactos). |
| `AudioBudgetStatus`/`Limits`/`Report` (`:45`, `:48`, `:56`) | Severidad (`Ok`/`Warning`/`Exceeded`) y límites. |
| `AudioPlan` (`:67`) | El plan del frame: asignación de los 4 canales. |
| `AudioMixer` (`:104`) | Compila los eventos a `AudioPlan` (lógica de asignación de canales). |

## Modos y canales — `audio_mode.hpp`

`AudioMode` (`audio_mode.hpp:22`) reparte las 4 voces entre SFX y música; `ChannelQuota`/`channel_quota(mode)` (`:30`, `:43`) da el reparto. `AudioConfig` (`:58`) + `valid_audio_config` (`:71`) validan; `apply_audio_config(cfg, set_mode, rollback, mixer)` (`:91`, `:102`) lo aplica con vuelta atrás. `kPalClock = 3546895` (`:113`) y `period_for_hz(hz)` (`:117`) convierten frecuencia a período de Paula. `valid_channel_mask` (`:18`, `:132`) valida la máscara de canales.

## SFX por software — `sfx_mixer.hpp`

`SfxMixer` (`sfx_mixer.hpp:217`) es la envoltura C++23 del **Audio Mixer 3.7 de Photon** (integrado nativamente): mezcla hasta `mixer_sw_channels` (4) muestras en **un** canal hardware de Paula. Las muestras van **preprocesadas** (cada byte en ±128/nº canales; longitud múltiplo del mínimo) — ver `docs/engine/audio/AUDIO_MIXER.md`.

| Tipo | Qué es |
|---|---|
| `LoopMode` (`:37`) | `Once`/`Loop`/`LoopOffset` (`MIX_FX_*`). |
| `MixerEffect` (`:72`) | Estructura que consume el ASM. |
| `SfxSample` (`:202`) | Muestra del mixer. |
| `SfxChannel` (`:207`) | Canal software (s32). |
| Constantes (`:20`) | `MixPal`/`MixNtsc`, `MixCh0..3`, `MixChFree`/`MixChBusy`. |

El ASM vive en `support/audio_mixer/` (ensamblado con VASM a ELF por `build-demo.sh`); es Amiga-only (usa VBR/interrupciones/DMACON).

## Afinado y feeder

`audio_tuning.hpp` reúne las tablas de afinado; `audio_feeder.hpp` es el **feeder de la IRQ de Paula** (nivel 4): se llama una vez por petición de DMA y avanza el audio. `wave_tables.hpp` aporta tablas de onda.

Volver al [índice de `audio/`](README.md) · [Referencia](../README.md) · [manual](../../README.md).
