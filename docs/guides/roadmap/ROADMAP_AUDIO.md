# Roadmap de audio (`eng::audio`)

Plan para completar la capa de audio del engine: modos y reparto de canales, reproductores de
música, integración con el mini-SO, **streaming digital desde disquete** y **decodificadores**.
Diseño en [`GAME_AUDIO.md`](../../engine/architecture/GAME_AUDIO.md),
[`AUDIO_MIXER.md`](../../engine/architecture/AUDIO_MIXER.md),
[`MUSIC_PLAYER.md`](../../engine/architecture/MUSIC_PLAYER.md) y
[`AUDIO_STREAMING.md`](../../engine/architecture/AUDIO_STREAMING.md).

## Principios

- **Componer, no duplicar.** El mixer de Photon (`AUDIO_MIXER.md`) y los reproductores
  (`MUSIC_PLAYER.md`) ya existen; el roadmap los **unifica** por modos y añade lo que falta.
- **HOST primero.** Los codecs y la lógica pura (reparto de canales, cabecera de stream, máquina de
  estados del buffer) se validan con tests HOST; el hardware, con demos build → run → analyze.
- **Sin heap ni excepciones** en el camino caliente; capacidad fija y buffers en Chip donde Paula
  los necesita.

## Fases

### A0 — Modos de audio y reparto de canales

- **Entregable**: `eng::audio::AudioMode` (`Game`/`GameSfxOnly`/`TitleOctaMED`/`Silent`),
  `AudioConfig`, `init`/`shutdown`/`set_mode`/`mode`; helpers `eng::audio::paula`
  (`period_for_hz`, `dmacon_set`/`clr`, `stop_channels`, `set_buffer`).
- **Detalle**: §7 de [`GAME_AUDIO.md`](../../engine/architecture/GAME_AUDIO.md).
- **Verificación**: **HOST-269** (planificado como HOST-240) — `channel_quota` reparte las máscaras
  correctas por modo sin solapar mixer/música, y `period_for_hz` acota 124..65535. La parada
  ordenada (`vol=0` + `DMACON`) la hace el backend Amiga.
- **Estado**: **entregado** (`eng/audio/audio_mode.hpp` + extensión de `AudioSystem` con
  `init(memory, cfg)`/`set_mode`/`mode`; HOST-269). Nota: el mixer de SFX tiene máscara **fija** en
  `mixer_config.i`, así que `GameSfxOnly` no reconfigura el mixer en runtime (documentado en el
  header).

### A1 — Reproductor OctaMED (títulos, 8 canales SW)

- **Entregable**: `eng::audio::OctaMedPlayer` envolviendo `KONEY/octamed_playroutines_amiga`;
  `MusicFormat::OctaMED`; modo `TitleOctaMED` que ocupa los 4 canales HW.
- **Detalle**: [`MUSIC_PLAYER.md`](../../engine/architecture/MUSIC_PLAYER.md).
- **Verificación**: demo de pantalla de título con módulo MED incrustado; gate visual de que suena
  y que al volver a `Game` el mixer+P61 recuperan sus canales.
- **Estado**: **parcial y operativo**. Hecho: ASM vendorizado (`support/music/octamed/` +
  `support/music/med.asm`) que **ensambla y enlaza**, envoltura `eng::audio::OctaMedPlayer` +
  `MusicFormat::OctaMED` (opt-in `-DENG_AUDIO_OCTAMED`), modo `TitleOctaMED`, banco de módulos
  (`assets/amiga/audio/`, 4 de KONEY) y la demo `274_octamed_probe`. **Con `OCTAMED_READY_FRAME=0`
  la música SUENA** y `_startmusic` retorna; **el modo que espera frames (READY en N) se cuelga** —
  el playroutine necesita su timing por IRQ y el bucle de espera del engine lo atropella.
  **Uso operativo**: `bash tools/audio/listen-octamed.sh <n> <s>` (sin `--warp`, deja el emulador
  vivo). **Cerrar el modo N** queda como mejora futura; el diagnóstico está en
  `docs/debugging/investigaciones/octamed-startmusic-hang.md` y la lección de proceso en
  `docs/guides/methodology/LECCION-CONTEXTO-DE-LA-FUENTE.md`. La demo de título (propiamente dicha)
  sigue pendiente.

### A2 — Integración con el mini-SO

- **Entregable**: `tick_frame()` en VBlank para los players frame-driven; mensajes opcionales
  `MsgType::MusicEnd` y `MsgType::AudioUnderrun`; regla "mixer por su IRQ, música por VBlank/CIA".
- **Detalle**: §8 de [`GAME_AUDIO.md`](../../engine/architecture/GAME_AUDIO.md).
- **Verificación**: **HOST-270** (planificado como HOST-241) — `AudioMsgEdges` emite `MusicEnd` /
  `AudioUnderrun` **una vez por evento** (flanco), no por buffer, y se re-arma al cesar.
- **Estado**: **entregado** (`MsgType::MusicEnd`/`AudioUnderrun` en `eng/os/message.hpp`;
  `AudioSystem::tick_frame(port)` + `notify_underrun()` + `P61Player::ended()`; `audio_events.hpp`;
  HOST-270). La música por CIA (Protracker) no se tickea en VBlank (la lleva su IRQ).

### A3 — Codec Delta + RLE (ByteRun1)

- **Entregable**: `eng/audio/pcm_codec.hpp` (`decode(comprimido, destino, tipo)`, freestanding, sin
  heap) con el formato **Delta + RLE**; decodifica directo a Chip.
- **Verificación**: **HOST-242** — un compresor de prueba genera vectores (silencios, rampas,
  ruido), el decodificador los reconstruye **byte a byte** y respeta el tamaño de destino.
- **Estado**: hecho (implementación y test en este repo).

### A4 — Descompresores ZX0 / aPLib

- **Entregable**: port de `unzx0_68000` y `aPLib` con el mismo contrato `decode`; cabecera de
  archivo (`AUZX`) con `compression` (0=ZX0, 1=aPLib, 2=delta+RLE).
- **Verificación**: **HOST-243** — vectores ZX0/aPLib generados en el host (compresor de
  referencia) se decodifican a la misma PCM; comparación byte a byte.
- **Estado**: **parcial**. Entregados y verificados en `pcm_codec`:
  - **ZX0** (`Codec::Zx0`, `eng/audio/zx0.hpp`, port de `dzx0.c` v2) — **HOST-271** con vector del
    compresor de referencia.
  - **Delta + ZX0** (`Codec::DeltaZx0`, sin pérdida) — preprocesado delta + ZX0; **HOST-324**.
  - **Fibonacci Delta** (`Codec::FibDelta`, IFF 8SVX, con pérdida 2:1, `eng/audio/fib_delta.hpp`) —
    **HOST-323**, contra el Apéndice C del estándar.

  **Pendiente**: aPLib, **IMA ADPCM 4-bit** y la cabecera contenedora `AUZX` (que consume
  `compression`). Los formatos exactos y la receta de compresión en PC están en
  `docs/engine/architecture/AUDIO_STREAMING.md` §7.1.

### A5 — Streaming digital desde disquete

- **Entregable**: `PcmStream` (doble/triple buffer) con la IRQ de audio cambiando de buffer y la
  descompresión en tarea de fondo; `file_read_async` para los chunks.
- **Detalle**: [`AUDIO_STREAMING.md`](../../engine/architecture/AUDIO_STREAMING.md).
- **Verificación**: **HOST-239** (con E/S simulada: llena N buffers, detecta *underrun*, termina en
  EOF) y **demo 209_audio_stream** (grabación continua desde disquete en hardware).
- **Estado**: **parcial**. Entregado: la **máquina de estados** de buffers `eng/os/stream.hpp`
  (`ChunkStream<NumBuffers>`: `request_mask`/`on_chunk_ready`/`advance`/`underrun`/`eof`; **HOST-257**)
  y `eng/audio/pcm_stream.hpp` (`PcmStream<NumBuffers>`: une `ChunkStream` + el codec y reparte
  llamador (E/S + `provide`) / IRQ (`advance` + `play_pcm`) / fin de stream; **HOST-239**). En
  hardware: la **IRQ de audio nivel 4** (`support/level4_irq.s` + `install_audio_service`) y la
  **programación de Paula por voz** (`PaulaAudio::set_buffer`/`start_channel`). La demo
  `272_audio_stream` (streaming desde RAM) queda **sin verificar**: la IRQ de audio dispara ~34× más
  rápido que `AUDxPER * AUDxLEN` (ver `docs/debugging/investigaciones/audio-stream-irq-rate.md`). Pendiente: resolver
  ese ritmo, el *swap* real en hardware y la E/S desde `trackdisk`.

### A6 — API unificada y ejemplo de juego

- **Entregable**: consolidar `init`/`set_mode`/`play_sfx`/`play_music`/`stop_*` como la superficie
  estable (junto con `GameAudio` para la política de juego) y un ejemplo de uso completo
  (boot → gameplay → título → pausa).
- **Verificación**: demo que ejercita todos los modos y la política de SFX sin tocar registros.
- **Estado**: **entregado**. La superficie estable es `AudioSystem` (`init`/`set_mode`/`play_sfx`/
  `play_music`/`stop_*`) + `GameAudio` (política: banco/cooldown/prioridad/ducking). Demos:
  **`217_audio_game_example`** (boot→título→gameplay→pausa con modos + SFX) y `216_audio_modes` +
  `062_game_audio`. Pendiente fino: el *handover* completo mixer↔OctaMED (ver A0).

## Tests y demos previstos

| ID | Tipo | Contenido |
|---|---|---|
| HOST-269 | test | Modos de audio y reparto de canales; `period_for_hz` (planificado como HOST-240). |
| HOST-270 | test | `MusicEnd`/`AudioUnderrun` (semántica, sin mensaje por buffer; planificado como HOST-241). |
| HOST-242 | test | Codec Delta + RLE (round-trip byte a byte). |
| HOST-271 | test | Descompresor ZX0 con vector del compresor de referencia (planificado como HOST-243); aPLib pendiente. |
| HOST-323 | test | Fibonacci Delta (IFF 8SVX): decoder contra el estándar + vector dorado; encoder `decode(encode(x))==x`. |
| HOST-324 | test | Delta + ZX0 sin pérdida: `differentiate`/`integrate_deltas` y `Codec::DeltaZx0` sobre vector ZX0 real. |
| HOST-239 | test | Streaming (`PcmStream<NumBuffers>`: doble buffer, underrun, EOF, chunk inválido) con E/S simulada. **Entregado**. |
| (por numerar) | demo | Grabación continua desde disquete con `PcmStream` (elegir nº libre del bloque D; `209` lo ocupa `209_reactive_loop`). |

## Riesgos y decisiones abiertas

- **Canales fijos del mixer.** El mixer de Photon es de configuración fija; el reparto por modos
  debe respetar `mixer_hw_mask`/`music_hw_mask` y no solaparlos nunca.
- **Módulos a 3 canales.** P61 debe limitarse a los canales de música; si el player no lo permite,
  el módulo se compone sin notas en el canal del mixer.
- **Orden de arranque.** Arrancar la música **antes** que el mixer (los players inicializan todos
  los canales).
- **Codec por defecto.** Delta+RLE es rápido pero comprime menos; ZX0/aPLib dan mejor ratio a
  cambio de un descompresor mayor. La elección es por archivo (`compression` en la cabecera).
- **IRQ de audio vs VBlank.** El mixer y el streaming usan la IRQ de Paula (nivel 4); la música de
  tracker frame-driven, el VBlank. No mezclar ambas vías en el mismo backend.

## Estado

La capa de juego (`GameAudio`), el mixer y P61/Protracker **ya existen** (demos 058–062).
Entregados: **A0** (modos y reparto de canales, HOST-269), **A2** (eventos
`MusicEnd`/`AudioUnderrun`, HOST-270), **A3** (codec Delta + RLE, HOST-242) y **A6** (ejemplo de
juego, demo `217_audio_game_example`). **A1** tiene la **infra lista** (ASM vendorizado + 
`OctaMedPlayer`) pero el **runtime `_startmusic` cuelga** (ver
`docs/debugging/investigaciones/octamed-startmusic-hang.md`). **A5** es parcial (`ChunkStream` HOST-257 +
`PcmStream` HOST-239; falta la E/S real y la demo en hardware).
**A4**: ZX0 entregado y verificado (HOST-271); aPLib pendiente.
