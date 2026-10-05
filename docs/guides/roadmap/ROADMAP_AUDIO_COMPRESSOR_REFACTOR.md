# Roadmap de refactor de audio host

Este roadmap reorganiza `host-tools/audio-compressor/` como una aplicación host portable para importar, analizar, comprimir y reproducir audio destinado a Amiga. El objetivo es que el núcleo batch no dependa de Win32, FFmpeg ni del backend Amiga, y que SDL3 sea la única dependencia externa obligatoria de la aplicación completa. Los importadores de formatos que no sean WAV/RAW deben ser extensiones explícitas, porque SDL3 no es un demultiplexor ni un decoder completo de MP3, OGG, FLAC o módulos tracker.

## Diagnóstico actual

```text
audio-compressor/src/main.cpp
├── CLI, JSON parcial y clasificación
├── rutas Windows y escritura binaria
├── invocación externa de FFmpeg
├── loader WAV heredado de host-tools/pack-pcm
├── codecs AUZX y métricas
├── HPSS/FFT
├── writer ACP1 v2 y v3 MVP
├── reconstrucción y comparación
└── reproducción SDL3 opcional
```

La aplicación funciona como primera vertical, pero `main.cpp` concentra demasiadas políticas. La compilación compila una única unidad de traducción que incluye ingestión, DSP, serializadores, parser del engine y reproducción; esto aumenta el tiempo de compilación, dificulta probar cada frontera y hace que una modificación de formato pueda afectar al flujo CLI completo.

`host-tools/pack-pcm/wav_loader.hpp` es una dependencia de implementación entre dos programas independientes. `sdl_host_io.hpp` mantiene otra ruta de lectura completa de ficheros y `main.cpp` contiene además escritura Win32 específica. El resultado son tres decisiones de E/S, dos loaders de fichero y ownership basado en `std::vector` completos, sin una fuente única para ventanas, límites y errores.

El reproductor SDL3 solo reproduce PCM normalizado y la lectura/mezcla host de ACP1 sigue pendiente. El soporte MP3/OGG/FLAC/MOD depende de ejecutar `ffmpeg`; por tanto la aplicación no es portable con SDL3 como única dependencia real. La opción estricta SDL3+runtime debe limitarse a WAV/RAW o incorporar decoders host con licencia y API estable dentro de un módulo separado.

HPSS reserva matrices proporcionales a la duración completa: espectro, magnitudes, medianas, pesos y señales reconstruidas. `ram_budget_bytes` evita exceder una estimación, pero no convierte el algoritmo en procesamiento por ventanas. El código de ACP1 v2 y v3 repite escritura little-endian, cálculo de extents y materialización de blobs; falta una capa binaria host común.

## Arquitectura objetivo

```text
┌──────────────────────────────────────────────────────────────────┐
│ app/ CLI, configuración, diagnóstico y selección de pipeline     │
└──────────────────────────────┬───────────────────────────────────┘
                               ▼
┌──────────────────────────────────────────────────────────────────┐
│ pipeline/ sample, music, candidates, metrics, reports            │
└──────────────┬───────────────────┬──────────────────┬────────────┘
               ▼                   ▼                  ▼
┌──────────────────────┐ ┌────────────────────┐ ┌─────────────────┐
│ io/ AudioSource      │ │ dsp/ WindowedView  │ │ codecs/ Registry │
│ WAV/RAW/importers    │ │ HPSS/STFT/resample │ │ AUZX/ACP1 codecs │
└──────────┬───────────┘ └──────────┬─────────┘ └────────┬────────┘
           ▼                        ▼                    ▼
┌──────────────────────────────────────────────────────────────────┐
│ format/ binary reader-writer, AUZX, ACP1 v2/v3                   │
└──────────────────────────────┬───────────────────────────────────┘
                               ▼
┌──────────────────────────────────────────────────────────────────┐
│ platform/ SDL3 playback, SDL3 I/O, optional external importers   │
└──────────────────────────────────────────────────────────────────┘
```

La aplicación debe depender de interfaces de valor y conceptos, no de herencia virtual. Una `AudioSource` host ofrece metadatos y `read(WindowRequest, Span<Sample8>)`; un `AudioSink` recibe chunks o secciones serializadas. El pipeline no conoce SDL3, FFmpeg ni Win32. El backend SDL3 se instancia en `main()` y solo implementa reproducción y, opcionalmente, E/S.

## Árbol propuesto

```text
host-tools/audio-compressor/
├── README.md
├── build.sh
├── include/audio_compressor/
│   ├── core/
│   │   ├── sample.hpp             # Sample8, SampleRate, Duration y spans de dominio
│   │   ├── error.hpp              # ErrorCode y Expected<T> host
│   │   ├── window.hpp             # WindowRequest, WindowCursor y límites
│   │   └── ownership.hpp          # buffers propietarios y vistas no propietarias
│   ├── io/
│   │   ├── audio_source.hpp       # concepto AudioSource y metadatos
│   │   ├── wav_source.hpp         # RIFF PCM por ventanas
│   │   ├── raw_source.hpp         # RAW PCM8 firmado por ventanas
│   │   ├── external_source.hpp    # frontera opcional FFmpeg/importadores
│   │   └── file_io.hpp            # SDL3 SDL_IOStream con fallback de test
│   ├── dsp/
│   │   ├── resampler.hpp
│   │   ├── hpss.hpp
│   │   ├── fft.hpp
│   │   └── metrics.hpp
│   ├── codecs/
│   │   ├── codec.hpp              # CodecConcept y CodecDescriptor
│   │   ├── registry.hpp            # std::array de codecs, sin switch disperso
│   │   ├── auzx_codec.hpp
│   │   └── segment_codec.hpp
│   ├── formats/
│   │   ├── binary_reader.hpp
│   │   ├── binary_writer.hpp
│   │   ├── auzx_writer.hpp
│   │   ├── acp1_v2_writer.hpp
│   │   └── acp1_v3_writer.hpp
│   ├── pipeline/
│   │   ├── sample_pipeline.hpp
│   │   ├── music_pipeline.hpp
│   │   ├── candidate_search.hpp
│   │   └── report.hpp
│   └── playback/
│       ├── player.hpp              # contrato PCM/medio reproducible
│       └── sdl3_player.hpp         # único backend SDL3
└── src/
    ├── main.cpp                    # composición de dependencias y CLI mínima
    ├── io/*.cpp
    ├── dsp/*.cpp
    ├── codecs/*.cpp
    ├── formats/*.cpp
    ├── pipeline/*.cpp
    └── playback/sdl3_player.cpp
```

La propuesta no obliga a convertir cada header en una biblioteca dinámica. Los algoritmos pequeños y los codecs parametrizados pueden seguir siendo templates o headers; la ingestión, HPSS, serialización y SDL3 deben tener `.cpp` para limitar recompilación y aislar dependencias. `host-tools/pack-pcm/` debe convertirse en un consumidor fino de `audio_compressor::io` o retirarse cuando la aplicación única cubra sus casos.

## Reglas de dependencias

| Capa | Puede incluir | No puede incluir |
|---|---|---|
| `core` | tipos estándar, `eng::Span` si es necesario | SDL3, Win32, FFmpeg, formatos concretos |
| `io/wav` y `io/raw` | SDL3 I/O o una abstracción de fichero | Win32 directo, pipeline, player |
| `dsp` | `<algorithm>`, `<cmath>`, FFT propia | SDL3, filesystem, CLI |
| `codecs` | contratos de formato y buffers | SDL3, Win32, FFmpeg |
| `formats` | binary reader/writer y codecs | player, HPSS, CLI |
| `pipeline` | interfaces anteriores | SDL3, Win32, `system()` |
| `playback/sdl3` | SDL3 | Win32 directo, FFmpeg |
| `importers/ffmpeg` | proceso externo aislado | formatos internos, player |

La variante estricta sin dependencias externas debe compilar WAV/RAW, AUZX, ACP1 y reproducción PCM con SDL3. FFmpeg debe quedar detrás de `AUDIO_COMPRESSOR_ENABLE_EXTERNAL_IMPORTERS`, con una prueba de disponibilidad y un error que indique qué formato requiere el módulo. No se debe afirmar que SDL3 proporciona decodificación de MP3, OGG, FLAC o MOD.

## C++23 y ownership

- Sustituir `const char*` de las APIs internas por `std::string_view`, `std::filesystem::path` o tipos de dominio; mantener `argv` solo en el adaptador CLI.
- Usar `std::span` en la capa host o un alias `SampleSpan`; reservar `eng::Span` para contratos que crucen al engine.
- Usar `std::expected<T, Error>` cuando el toolchain host lo soporte y un `Result<T>` host pequeño cuando se quiera conservar compatibilidad; no devolver `bool` sin diagnóstico en las capas internas.
- Usar `std::unique_ptr` únicamente para recursos RAII de SDL3 (`SDL_AudioStream`, `SDL_IOStream`) mediante deleters; no usar `shared_ptr` sin ownership compartido real.
- Mantener buffers propietarios como `std::vector` o `std::pmr::vector` en host y pasar vistas no propietarias a codecs y DSP.
- Definir `template <class Source> concept WindowSource` y `template <class Codec> concept RoundTripCodec`; resolver el codec seleccionado en setup con un descriptor de `std::array`, no mediante virtuales ni un `switch` repetido en cada pipeline.
- Parametrizar tamaño de ventana, capacidad de scratch y semántica de salida como parámetros `constexpr` cuando sean propiedades del algoritmo; mantener frecuencia, duración y codec como valores runtime.
- Usar `std::ranges` y `std::span` donde hagan el recorrido más claro, sin forzar templates en HPSS si la implementación depende de `float` y FFT host.
- Centralizar endianess, extents y validación de overflow en `BinaryReader`/`BinaryWriter`; ningún writer debe repetir `wr16`/`wr32`.

## Roadmap

## Estado de la refactorización

| Fase | Estado | Evidencia y límite actual |
|---|---|---|
| R0 | Parcialmente ejecutada | El diagnóstico y la matriz de dependencias están documentados en esta ficha; falta convertir las reglas en gates automáticos de CI. |
| R1 | Parcialmente ejecutada | `include/audio_compressor/io/file_io.hpp` centraliza lectura/escritura, `sdl_player.hpp` usa `unique_ptr` con deleter SDL3 y `main.cpp` ya no incluye Win32; aún quedan `std::FILE`, `system()` y rutas temporales en el pipeline heredado. |
| R2 | En curso | `io/wav_source.hpp` y `io/raw_source.hpp` leen ventanas; `dsp/resampler.hpp` conserva fase y muestra de borde; SAMPLE forzado con codec explícito usa `write_auzx_windowed` y `formats/auzx_sink.hpp`. Quedan remuestreo conectado al pipeline, `auto`/MUSIC y el cierre del test HOST-420. |
| R3 | Parcialmente ejecutada | `codecs/registry.hpp` centraliza los encoders AUZX y `formats/binary.hpp` se usa en writers ACP1 v2/v3; falta migrar todos los lectores/parsers y eliminar helpers duplicados del engine/host. |

### R0 — Contrato y medición

- Congelar la matriz de entradas: WAV/RAW obligatorios; FFmpeg/MOD opcionales; P61/MED/AHX fuera del núcleo.
- Medir dependencias reales de cada variante con `ldd`/`objdump -p`, compilación sin SDL3 y compilación con SDL3 estático.
- Definir `ErrorCode`, `Sample8`, `SampleRate`, `WindowRequest` y el contrato de ownership.
- Añadir un test de arquitectura que impida incluir `windows.h`, invocar `system()` o incluir SDL3 desde `pipeline`, `dsp`, `codecs` y `formats`.

### R1 — Extraer la frontera de plataforma

- Mover lectura/escritura y temporales a `io/file_io`; sustituir Win32 y `std::FILE` disperso por SDL3 `SDL_IOStream` con un adaptador de test.
- Encapsular SDL3 en `SdlAudioPlayer` RAII; quitar la macro de implementación de los headers de pipeline.
- Mantener el fallback sin SDL3 solo para tests y conversión batch si se decide conservarlo; no duplicar dos implementaciones de fichero.
- Criterio: el núcleo batch compila en Linux, Windows y macOS sin `windows.h`; solo `playback/sdl3` conoce SDL3.

### R2 — Separar ingestión y procesamiento por ventanas

- Implementar `WavSource` y `RawSource` con lectura incremental, metadatos y remuestreo con estado entre ventanas.
- Rehacer sample pipeline para codificar AUZX incrementalmente y escribir índice/payload sin conservar PCM completo.
- Rehacer HPSS por bloques con solapamiento y estado; declarar el presupuesto de memoria de FFT y buffers de borde.
- Reemplazar el guardia actual `fits_memory_budget` por una reserva de scratch comprobable.
- Criterio: un fichero de duración arbitraria usa memoria acotada por ventana, scratch y buffers de salida; HOST-376/378 se conectan al pipeline real.

### R3 — Consolidar codecs y formatos

- Extraer `CodecConcept`, `CodecDescriptor` y registry compile-time para `none`, DeltaRLE, Fibonacci e IMA.
- Compartir `BinaryReader`/`BinaryWriter` entre AUZX, ACP1 v2 y ACP1 v3.
- Separar el parser Amiga freestanding de los writers host: el host no debe incluir más engine del necesario y el player Amiga no debe depender de STL.
- Añadir validación de chunks impares, semillas, límites, CRC y round-trip a cada descriptor.
- Criterio: cada codec tiene un test host, un vector binario y una ruta de análisis de codegen cuando exista implementación ASM.

### R4 — Pipelines y selección

- Extraer `SamplePipeline`, `MusicPipeline`, `CandidateSearch` y `ReportWriter` de `main.cpp`.
- Hacer que `auto` compare tamaño, error, RAM, CPU estimada y coste de reproducción; persistir todas las candidatas y la configuración resuelta.
- Hacer que ACP1 v2 use el mismo `WindowSource` y el mismo registry de codecs que AUZX.
- Implementar reader host ACP1 para reproducir y comparar ACP1, no solo AUZX.
- Criterio: `main.cpp` compone dependencias, traduce CLI y devuelve errores; no contiene algoritmos de DSP, formato ni ownership de recursos.

### R5 — Importadores opcionales

- Mantener WAV/RAW como base sin dependencias adicionales.
- Encapsular FFmpeg en un proceso/importer opcional con rutas temporales RAII, códigos de error y hash de la conversión.
- Documentar MOD como renderizado a PCM, no como preservación de patrones o stems; añadir P61/MED/AHX solo mediante módulos independientes si existe un caso de uso.
- Evaluar una futura dependencia embebida permisiva solo si elimina FFmpeg sin duplicar varios decoders y sin aumentar el ejecutable base de forma injustificada.

### R6 — ACP1 v3 y reproducción

- Completar el MVP actual con codecs por segmento, luego cues, envolventes, codebooks y wavetables; la primera vertical host de síntesis aditiva ya genera y reproduce ACP1 v3 por ventanas.
- Implementar un `Acp1HostPlayer` que consuma ventanas ACP1 con el mismo timeline que el player Amiga.
- Separar el planner de voces del formato; el writer no debe conocer Paula, Mixer ni IRQ.
- Validar la demo 280 en WinUAE después de resolver el wrapper de build, y añadir pruebas positivas/negativas de coexistencia con mixer/tracker.
- Objetivo de máximos: separar instrumentos mediante `F0`, parciales, envolventes, ataques y residual, probar hipótesis alternativas y sintetizar las unidades durante la reproducción en vez de almacenar toda la señal PCM. El contrato y las cinco fases están en [`AUDIO_COMPRESSION.md`](../../engine/architecture/AUDIO_COMPRESSION.md#objetivo-de-máximos-separación-instrumental-y-síntesis-durante-la-reproducción).
- El planner debe asignar primero `AUD1..AUD3`, después las cuatro voces software de `AUD0`, y solicitar OctaMED para concurrencia superior a siete; las pistas requeridas que no quepan se rechazan de forma explícita.
- La evaluación host expone MSE, SNR, pico, correlación entre pistas, exportación WAV por pista y escucha SDL3; el fallback actual declara `fallback=octamed` y queda pendiente conectar una playroutine MED dinámica.

### R7 — Integración y eliminación de deuda

- Retirar o convertir `host-tools/pack-pcm` en wrapper fino sobre la biblioteca host común.
- Eliminar `acp1_writer.hpp` y `acp1_v3_writer.hpp` duplicados cuando los writers estén en `formats/`.
- Eliminar includes de Win32 y `SDL3` fuera de sus módulos autorizados.
- Publicar matriz de plataformas, dependencias, tamaño de ejecutable, memoria máxima y tiempos de cada pipeline.

## Evidencia actual

- HOST-420 valida lectura WAV por ventanas, downmix PCM8 y remuestreo stateful entre ventanas. El runner host usa el linker del runtime seleccionado y enlaza estáticamente `libgcc` y `libstdc++` para evitar mezclar runtimes MSYS2/UCRT en Windows.
- La ruta SAMPLE windowed se ha probado con un WAV PCM8 de 11025 Hz, ventanas de 5 muestras, salida a 22050 Hz y codec `none`. El informe resultante valida 64 muestras, `round_trip_ok: true`, `mse_pcm8: 0` y `peak_error: 0`.

## Conclusiones de auditoría del modelo

### Abstracciones que representan bien el dominio actual

- `WavSource` y `RawSource` representan correctamente la lectura incremental mínima de SAMPLE (`sample_rate`, `frames`, `read`) y `SamplePipeline` compone esa operación con remuestreo, codec, round-trip y `AuzxSink` sin retener el PCM completo (`host-tools/audio-compressor/include/audio_compressor/io/`, `pipeline/sample_pipeline.hpp`).
- `AuzxSink` representa la propiedad del archivo AUZX en construcción, incluida la reserva de índice y su parcheo final; `BinaryWriter` evita repetir la codificación little-endian en las rutas host de AUZX y ACP1.
- `Descriptor` expresa la selección compile-time de codecs AUZX y distingue codecs con pérdida, mientras que `ReportWriter` separa la serialización de métricas del pipeline (`codecs/registry.hpp`, `report/report_writer.hpp`).

### Brechas del modelo

| Área | Estado observado | Abstracción necesaria |
|---|---|---|
| Fuentes | WAV y RAW comparten un contrato implícito, sin tipo de dominio común ni metadatos de formato/canales | `AudioSource` o `WindowSource` con `AudioFormat`, `sample_rate`, `frames` y `read` |
| Codecs | El registry solo contiene nombre, id y `lossy`; la codificación, decodificación y restricciones de chunk siguen repartidas entre `main.cpp` y headers del engine | `CodecDescriptor` con encode/decode, capacidad de round-trip, paridad, semilla/estado y coste estimado |
| MUSIC | La construcción de unidades, eventos, deduplicación, HPSS y reconstrucción ACP1 permanece en `main.cpp` | `MusicPipeline`, `Timeline`, `UnitDictionary` y `Acp1Writer` host separados |
| Reproducción | Se reproduce PCM/AUZX, pero ACP1 no tiene reader/player host equivalente | `Acp1HostPlayer` basado en ventanas y timeline compartido con el writer |
| E/S | `file_io.hpp` decide SDL3 o streams estándar dentro del mismo módulo; SDL3 no está completamente aislado como backend | `FileReader`/`FileWriter` host y adaptador SDL3 separado de la lógica de formatos |
| Configuración y métricas | `Config` y `ConversionStats` viven en el ámbito anónimo de `main.cpp`; `ReportWriter` depende de sus campos por plantilla estructural | Tipos host de dominio estables (`SampleOptions`, `MusicOptions`, `ConversionReport`) |
| Errores y commit de salida | `AuzxSink` deja un archivo parcial si una fase posterior falla y `ReportWriter` ignora errores de apertura/escritura | Resultado de error explícito y sink transaccional con abort/commit |
| Memoria MUSIC | `HpssResult`, stems, unidades y buffers de mezcla son vectores completos; `--ram-budget` solo estima el límite | Pipeline MUSIC por ventanas con scratch declarado y presupuesto comprobable |

### Decisión de diseño

No se debe introducir un `AudioAsset` universal que mezcle fuente, PCM, unidades comprimidas, timeline y reproducción. El modelo correcto son capas separadas: `AudioSource` produce ventanas normalizadas, `CodecDescriptor` transforma chunks, `AuzxSink` o `Acp1Writer` materializa formatos, y los players consumen readers de esos formatos. La representación común entre SAMPLE y MUSIC debe ser el contrato de ventanas y metadatos, no la propiedad de todos los buffers.

### Orden de trabajo derivado

1. Extraer `AudioSource`, `AudioFormat`, `SampleOptions` y `ConversionReport` del ámbito de `main.cpp`.
2. Completar `CodecDescriptor` con operaciones y restricciones; mover la selección de candidatos a `CandidateSearch`.
3. Extraer `MusicPipeline`, timeline, diccionario de unidades y `Acp1Writer` host antes de intentar optimizar HPSS.
4. Implementar `Acp1HostPlayer` y validar la equivalencia de timeline con el player Amiga.
5. Separar el backend SDL3 de `io/file_io.hpp` y hacer transaccionales los sinks e informes.

### Estado de la implementación

- Completado: `domain/audio_types.hpp` define `AudioFormat`, `WindowSource`, `ApplicationOptions` y `ConversionReport`; WAV y RAW exponen el mismo contrato estático y `SamplePipeline` lo consume sin dispatch virtual.
- Completado: `Descriptor` declara pérdida, round-trip exacto y paridad de chunk; `CandidateSearch` centraliza la elección por tamaño y el test host cubre Delta+RLE, Fibonacci e IMA.
- Parcial: `MusicPlan` y `MusicPipeline` separan la deduplicación de unidades, la timeline y el writer ACP1 de la llamada CLI; HPSS y la ingestión windowed de stems todavía se encuentran en `main.cpp` y requieren la extracción completa de R4.
- Completado: `Acp1HostPlayer` consume ACP1 v1/v2 por ventanas mediante `media::mix_window`; HOST-420 valida una composición mono reconstruida por el player. La reproducción SDL3 de ACP1 sigue pendiente.
- Completado: `file_io.hpp` usa la E/S estándar de batch y `sdl_file_io.hpp` contiene el adaptador SDL3 opcional; la lógica de conversión ya no selecciona SDL3 mediante una macro transitiva.
- Completado: `AuzxSink` escribe en un archivo temporal y solo reemplaza la salida pública tras `finalize`; el destructor elimina temporales abandonados.
- Parcial: `hpss_windowed_pcm` conecta HPSS con la ruta MUSIC, reutiliza el buffer de entrada y une los solapes mediante pesos lineales normalizados. La fuente PACK-PCM todavía entrega cada stem completo y `hpss()` reserva sus buffers FFT internos por ventana; ambas acumulaciones quedan pendientes de una workspace/source plenamente streaming.
- Contrato Paula/mixer aplicado: el plan reserva tres destinos directos (`AUD1..AUD3`) y cuatro destinos virtuales en la salida del mixer (`AUD0`); los chunks de tracks mixer se limitan a `-32..31` para que la suma de cuatro voces no desborde el mixer Photon. El planificador de concurrencia y la convivencia de IRQ siguen siendo responsabilidades del runtime Amiga.
- Política de salida: SAMPLE corto y sencillo se prepara para el mixer con amplitud por voz segura (`-32..31`); MUSIC con pitch o volumen variable se restringe a tres tracks Paula directos y falla antes de publicar ACP1 si necesita más.
- `tools/bench-audio-compressor.mjs` permite comparar tiempo de preparación por codec/chunk/ruta; la memoria RSS no se declara como evidencia portable y debe medirse con la herramienta nativa del entorno.
- `WavStemSource` ya permite leer un canal intercalado por ventanas; la integración completa de MUSIC debe sustituir el `WavStems` completo por este proveedor antes de considerar cerrado el presupuesto de memoria.
- `MusicPipeline::preflight` valida la política de tres voces Paula y estima buffers Chip/Fast antes de serializar; sus presupuestos deben pasar a configuración explícita cuando el backend de memoria los publique.

## Criterios de cierre

- El núcleo batch compila sin SDL3, Win32 ni FFmpeg; la aplicación completa añade únicamente SDL3 y los importadores seleccionados explícitamente.
- No hay `system()`, `windows.h`, `std::FILE` ni ownership SDL3 sin RAII en las capas internas.
- La ingestión sample y la evaluación de candidatas funcionan por ventanas con memoria acotada.
- AUZX, ACP1 v2 y ACP1 v3 comparten reader/writer binario y tests de límites sin compartir código incompatible con el runtime Amiga.
- `main.cpp` solo compone CLI, configuración, source, pipeline, reporter y player.
- Cada dependencia opcional aparece en un módulo y en una prueba de build, nunca como include transitivo de la aplicación completa.
