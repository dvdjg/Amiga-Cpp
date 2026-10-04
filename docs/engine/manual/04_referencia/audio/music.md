# Referencia — música, codecs y streaming

Música de tracker (reproductores asm de la demoscene) y streaming de PCM desde disco.

## Música — `music_player.hpp`, `audio_system.hpp`

`MusicModule` (`music_player.hpp:22`) es una vista `Span` a la memoria del módulo (sin puntero crudo). Los reproductores:

| Reproductor | Qué es |
|---|---|
| `P61Player` (`music_player.hpp:126`) | Envoltorio del reproductor **P61** (Photon/Scoopex): API `P61_Init`/`P61_Music`/`P61_End`/`P61_SetPosition` + `P61_ControlBlock`. |
| `PtPlayer` (`:282`) | Reproductor **ProTracker** (`.mod`). |
| `OctaMedPlayer` (`:351`) | Reproductor **OctaMED**. |

`p61_needs_sample_buffer(module)` (`:29`) / `p61_sample_buffer_size(module)` (`:44`) detectan si el módulo P61 tiene los samples **empaquetados** (exige buffer de descompresión; el tamaño está en el offset 4 LE). Los punteros de registro (`A0/A1/A2`) quedan en la capa interna. El ASM vive en `support/music/`.

`AudioSystem` (`audio_system.hpp:67`) detecta el **formato** de un módulo por su cabecera (`MusicFormat`, `detect_music_format`, `:45`) para `play_music(module)` sin especificar formato.

## Codecs — `pcm_codec.hpp`, `media.hpp`, `acp1`, `aplib`, `fib_delta`, `ima_adpcm`, `auzx`

| Codec | Qué es |
|---|---|
| `pcm_codec.hpp` | PCM 8-bit con signo: Delta + RLE (ByteRun1), freestanding. `Codec` (`:37`), `decode` (`:76`), `encode` (`:148`, `:210`). |
| `fib_delta.hpp` | Fibonacci Delta (IFF 8SVX, `sCompression = 1`), 8-bit con pérdida. |
| `ima_adpcm.hpp` | IMA/DVI ADPCM 4-bit, con pérdida. |
| `aplib.hpp` | Descompresor aPLib (Jørgen Ibsen; `apultra`). |
| `acp1.hpp`/`acp1_v3.hpp`/`acp1_stream.hpp` | Codec ACP1. |
| `auzx.hpp` | Contenedor AUZX: cabecera + índice de chunks para PCM comprimido. |
| `asm_codec.hpp` | Rutinas de descompresión en ASM 68000 (`support/codec_asm.s`). |

`media.hpp` es la **interfaz de medios**: punto único para reconocer un medio (`Container`, `:32`) y despachar su lectura. `Info` (`:39`), `open(blob, out)` (`:54`), `chunk_samples`/`chunk_data`/`decode_chunk`/`decode_track_window`/`mix_window` (`:109`–`:194`).

## Streaming — `pcm_stream.hpp`, `media_stream_backend.hpp`

`PcmStream<NumBuffers>` (`pcm_stream.hpp:29`) es el **streaming de PCM desde disquete**: une la máquina de estados de buffers (`os::ChunkStream`) con el mix. `MediaStreamBackend<Source>` (`media_stream_backend.hpp:25`) adapta una fuente de medio. Ver `docs/engine/architecture/AUDIO_STREAMING.md`.

Volver al [índice de `audio/`](README.md) · [Referencia](../README.md) · [manual](../../README.md).
