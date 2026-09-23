# Tests HOST — audio

Categoría `audio` de la batería host (L1). El índice de categorías está en [../README.md](../README.md) y la taxonomía en [docs/testing/TAXONOMY.md](../../../docs/testing/TAXONOMY.md).

## Catálogo

| ID | Test | Qué cubre |
|----|------|-----------|
| HOST-005 | [audio](005_audio/README.md) | `eng::audio::AudioMixer`: asignación de canales de Paula (SampleEvent → AudioPlan) — paso 7 de `ENGINE_DESIGN.md` §5. |
| HOST-008 | [game_audio](008_game_audio/README.md) | `eng::audio::SampleBank` + `allow_trigger`: banco de muestras y política de voces (cooldown + límite de instancias) — capa de audio de juego. |
| HOST-009 | [wave_tables](009_wave_tables/README.md) | `eng::audio::sine_byte`/`triangle_byte`/`square_byte`: tablas de forma de onda 8-bit (enteras, sin float). |
| HOST-239 | [audio_stream](239_audio_stream/README.md) | Streaming PCM (`eng/audio/pcm_stream.hpp`): `PcmStream` sobre `ChunkStream` + codec real (Delta+RLE); doble buffer, underrun, EOF y chunk inválido con E/S simulada. |
| HOST-242 | [pcm_codec](242_pcm_codec/README.md) | Codec PCM Delta + RLE (`eng/audio/pcm_codec.hpp`): round-trip byte a byte, ratio y rechazos (codec/destino/truncado). |
| HOST-269 | [audio_mode](269_audio_mode/README.md) | Modos de audio (`audio_mode.hpp`): reparto de los 4 canales de Paula por modo (`channel_quota`, sin solape) y `paula::period_for_hz` acotado. |
| HOST-270 | [audio_events](270_audio_events/README.md) | Eventos de audio (`audio_events.hpp`): `AudioMsgEdges` emite `MusicEnd`/`AudioUnderrun` una vez por evento (flanco), no por buffer. |
| HOST-271 | [zx0](271_zx0/README.md) | Descompresor ZX0 (`eng/audio/zx0.hpp`, port de `dzx0.c` v2) verificado contra un vector del compresor de referencia; dispatch `pcm_codec` (`Zx0`/`DeltaRle`/`APLib`). |
