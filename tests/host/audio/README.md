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
| HOST-357 | [fib_delta](357_fib_delta/README.md) | Fibonacci Delta (IFF 8SVX `sCompression=1`, `eng/audio/fib_delta.hpp`): decoder contra el estándar (Apéndice C) + vector dorado, encoder `decode(encode(x))==x`, `Codec::FibDelta`. |
| HOST-358 | [delta_zx0](358_delta_zx0/README.md) | Delta+ZX0 sin pérdida: `differentiate`/`integrate_deltas` inversas y `Codec::DeltaZx0` sobre un flujo ZX0 real de referencia. |
| HOST-359 | [ima_adpcm](359_ima_adpcm/README.md) | IMA ADPCM 4-bit (`eng/audio/ima_adpcm.hpp`): decodificador contra el estándar IMA/DVI y round-trip con error acotado; `Codec::ImaAdpcm`. |
| HOST-360 | [auzx](360_auzx/README.md) | Contenedor AUZX (`eng/audio/auzx.hpp`): cabecera de 32 bytes + índice de chunks (offset/tamaño), parseo y rechazos. |
| HOST-361 | [media](361_media/README.md) | Interfaz de medios (`eng/audio/media.hpp`): reconoce PCM crudo/AUZX y decodifica por chunk (`decode_chunk`), con el códec de `pcm_codec`. |
| HOST-362 | [aplib](362_aplib/README.md) | Descompresor aPLib (`eng/audio/aplib.hpp`) contra un flujo real de `apultra`; dispatch `Codec::APLib` y rechazos. |
| HOST-363 | [pcm_stream_seek](363_pcm_stream_seek/README.md) | `PcmStream<3>` (triple buffer) y `seek(chunk)`: reposiciona el stream en un chunk del índice. |
| HOST-370 | [audio_plan](370_audio_plan/README.md) | Presupuesto de `AudioPlan` (`eng/audio/audio.hpp`): `AudioBudget`/`Limits`/`Report` (voces DMA + palabras), análogo de `BlitBudget`. |
| HOST-371 | [sound_queue](371_sound_queue/README.md) | `SoundQueue` (`eng/audio/sound_queue.hpp`): `SampleEvent` sobre el mecanismo `eng::IntentQueue` (`eng/core`) → `AudioPlan`; la misma cola que blit/dibujo. |
| HOST-372 | [audio_feeder](372_audio_feeder/README.md) | `AudioFeeder` (`eng/audio/audio_feeder.hpp`): feeder IRQ-apto (nivel 4) con `irq`/`swaps`/`underrun`; alimentado a tiempo `irq == swaps`, 0 underruns. |
| HOST-373 | [sound_planner](373_sound_planner/README.md) | `SoundPlanner` (`eng/audio/sound_planner.hpp`): une intención (`SoundQueue`) + plan (`AudioPlan`) + aviso (`Msg IntentDone`/`AudioUnderrun`) con una llamada por frame. |
| HOST-374 | [wav_loader](374_wav_loader/README.md) | Loader host-only de WAV PCM lineal mono/estéreo de 8/16 bits a PCM8 mono con signo y preservación ordenada de canales multicanal como stems. |
| HOST-375 | [stream_intent](375_stream_intent/README.md) | Intención portable de reproducción continua por recurso, compatible con backend Paula o mixer. |
| HOST-376 | [stream_window](376_stream_window/README.md) | Evaluación host por ventanas reutilizables con presupuesto de 6 GiB. |
| HOST-377 | [media_stream_backend](377_media_stream_backend/README.md) | Adaptador de `StreamIntent` a recursos `media::Info`, independiente de Paula/mixer. |
| HOST-378 | [codec_search](378_codec_search/README.md) | Evaluación con reconstrucción y búsqueda del mejor candidato por ventanas. |
| HOST-379 | [audio_tuning](379_audio_tuning/README.md) | Parámetros y métricas de audio genéricos, probados con `float` y `Fixed<s32,16>`. |
| HOST-381 | [playback_api](381_playback_api/README.md) | API común de reproducción y control por `PlaybackHandle`. |
| HOST-382 | [audio_compressor_cli](382_audio_compressor_cli/README.md) | Aplicación única host: WAV multicanal a AUZX sample y ACP1 v2, eventos por bloques, round-trip, HPSS y canales FLAC vía FFmpeg. |
| HOST-387 | [acp1](387_acp1/README.md) | Parser freestanding/encoder ACP1 v1/v2, decoder y mixer por ventanas, feeder triple-buffer, deduplicación exacta y HPSS. |
| HOST-389 | [acp1_v3](389_acp1_v3/README.md) | MVP ACP1 v3: directorio de secciones, unidades PCM, segmentos, payloads, tracks, eventos y rechazos estructurales. |
| HOST-395 | [synth_renderer](395_synth_renderer/README.md) | Renderer entero PCM8 de voces aditivas: parciales armónicos, formas estándar y fase continua entre ventanas. |
| HOST-420 | [wav_window_source](420_wav_window_source/README.md) | `WavSource`: lectura WAV PCM8 multicanal por ventanas y downmix incremental. |
| HOST-421 | [synth_voice_plan](421_synth_voice_plan/README.md) | Planificador de rutas de síntesis: tres voces Paula, cuatro mixer y fallback explícito OctaMED. |
| HOST-422 | [synth_playback_plan](422_synth_playback_plan/README.md) | Preparación de ventanas PCM8 y metadatos de periodo/volumen para Paula o mixer. |
| HOST-423 | [acp1_v3_additive](423_acp1_v3_additive/README.md) | Writer y vistas ACP1 v3 aditivo: unidad instrumental, parciales, notas, pitch y ganancia. |
| HOST-424 | [harmonic_separation](424_harmonic_separation/README.md) | Separador host experimental: candidatos F0, energía de parciales y modelos instrumentales aditivos. |
| HOST-425 | [spectral_prototypes](425_spectral_prototypes/README.md) | Separador host experimental por STFT: prototipos espectrales, activación temporal, desplazamiento de bins y variantes de 3/8 prototipos. |
