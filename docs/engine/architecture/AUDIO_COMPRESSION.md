# Diseño de audio comprimido para Amiga

El pipeline de audio convierte fuentes de PC en PCM mono de 8 bits con signo y las empaqueta en `AUZX`, el contenedor que consume el engine Amiga. El formato, los codecs y el decoder se comparten entre `host-tools/pack-pcm` y `engine/include/eng/audio/`; así el fichero producido en PC tiene el mismo contrato que el reproductor de Amiga. `pack-pcm` acepta RAW PCM8 firmado y WAV PCM lineal mono o estéreo de 8/16 bits.

## Objetivos

- Producir audio mono PCM8 firmado, compatible con Paula y con el streaming por chunks del engine.
- Probar configuraciones de codec, predicción, cuantización y tamaño de bloque sobre un corpus reproducible.
- Mantener el decoder Amiga freestanding, sin STL, excepciones ni heap en el camino de reproducción.
- Permitir que la utilidad lea WAV y RAW inicialmente, y delegar la extracción de audio de vídeo descargado a `ffmpeg`/`yt-dlp` sin incorporar sus binarios al repositorio.
- Comparar el resultado decodificado con la señal normalizada y registrar tamaño, ratio y métricas de error.

## Capas

```text
fuente WAV/RAW/video descargado
          │  ingestión PC
          ▼
PCM normalizado ── análisis ── búsqueda de parámetros
          │                         │
          └──────────────► encoder AUZX ──► archivo para Amiga
                                             │
                         índice/chunk ──────┤
                                             ▼
                 parser freestanding + decoder C++23/ASM
                                             │
                                             ▼
                                      PCM8 en Chip RAM → Paula
```

El empaquetador C++ vive en `host-tools/pack-pcm/` y reutiliza los headers del engine. El pipeline Node `tools/audio/pack-auzx.mjs` cubre la generación Fibonacci Delta sin compilar C++. El contenedor portable está definido por `eng/audio/auzx.hpp`; `eng/audio/media.hpp` ofrece el punto único de reconocimiento y decodificación por chunk. Las rutinas críticas tienen referencia C++ y variantes ASM 68000 bajo el mismo contrato.

## Contenedor AUZX

Todos los enteros se escriben little-endian para que el parser sea explícito y estable entre PC y 68000.

```text
Cabecera fija, 32 bytes
  0..3    magic "AUZX"
  4       versión = 1
  5       codec global
  6..7    frecuencia de muestreo
  8..9    canales = 1
  10      bits = 8
  12..15  muestras PCM totales
  16..17  muestras por chunk
  18..19  número de chunks
  20..23  offset del índice
  24..27  offset del primer payload
  28..31  checksum opcional

Índice por chunk, 8 bytes
  0..3    offset absoluto del payload
  4..7    tamaño comprimido
```

El índice permite `seek(chunk)` y evita leer chunks anteriores. `PcmStream` usa el tamaño descomprimido configurado en la cabecera para llenar buffers de Chip RAM. El formato v1 usa mono PCM8; cualquier cambio de layout requiere una nueva versión.

## Modos de compresión

La selección se hace por chunk, comparando tamaño y calidad:

| Modo | Estado | Uso previsto |
|---|---|---|
| PCM crudo | implementado | fallback y referencia de calidad |
| Delta+RLE | implementado | codec portable rápido |
| ZX0 | implementado | PCM directo y residual Delta+ZX0 |
| aPLib | implementado | codec LZ alternativo |
| Fibonacci Delta | implementado | 4 bits por delta, con pérdida |
| IMA ADPCM 4-bit | implementado | compresión con pérdida y decoder ASM previsto |
| Residual armónico | roadmap | tonos estables y síntesis por tabla |

El análisis armónico no se almacena como metadato decorativo: solo se selecciona si sintetizar el tono, codificar el residual y guardar sus parámetros produce un coste total menor o una mejora de calidad justificada. El decoder leerá siempre el modo desde la cabecera del chunk.

## Análisis y error

El encoder debe conservar la señal normalizada como referencia y verificar el round-trip. Las métricas mínimas son MSE, RMS del error, SNR, pico absoluto y ratio `bytes_AUZX / bytes_PCM`. Para el material musical se añadirá ponderación perceptual por energía y bandas, sin usar una métrica perceptual para ocultar un error de reconstrucción byte a byte en modos lossless.

Los parámetros explorables serán frecuencia, canales de entrada, tasa de salida, tamaño de chunk, predictor, bits de cuantización, escala, dithering, noise shaping, preénfasis, codec y umbrales de tonalidad. El entrenamiento host trabaja por ventanas reutilizadas y tiene un presupuesto declarado de 6 GiB; la duración del audio no determina el consumo de RAM. Cada ensayo debe guardar la configuración completa y el hash de la entrada bajo `out/playground/audio-compressor/`. El evaluador solo puede publicar MSE/SNR si el adaptador ha reconstruido la ventana; el tamaño comprimido por sí solo no es una métrica de calidad.

La reproducción se solicita con una `StreamIntent` (`eng/audio/stream_intent.hpp`): el juego aporta un `AudioStreamId`, volumen, bucle y número de buffers, y el backend decide si materializa una voz DMA de Paula o una voz del mixer. `MediaStreamBackend` valida el medio reconocido por `media::Info`; `StreamingAudioPlanner` reutiliza la cola genérica y los eventos `IntentDone`. `PcmStream` y `AudioFeeder` siguen siendo la implementación de bajo nivel; la intención es la interfaz cómoda y no bloqueante.

## Restricciones del decoder Amiga

- La IRQ de audio solo cambia buffers y marca trabajo pendiente; la descompresión se realiza en el bucle o en una tarea cooperativa.
- El payload puede residir en Fast RAM, pero el PCM que lee Paula debe terminar en Chip RAM.
- El decoder C++23 será la referencia portable; los bucles medidos se podrán portar a ASM 68000 después de disponer de un test de equivalencia byte a byte y una auditoría `asm-audit.mjs`.
- La síntesis armónica usará tabla de onda y acumulador de fase; el objetivo A500 inicial es fundamental más un armónico, con un máximo de dos armónicos en la primera implementación.

## Fuentes externas y licencias

ZX0 se integra mediante el decoder del engine, atribuido a Einar Saukas. Los depackers ASM ZX0 y aPLib incorporados desde Emmanuel Marty conservan licencia zlib. IMA ADPCM sigue el estándar IMA/DVI. `ffmpeg` y `yt-dlp` son dependencias opcionales del entorno del usuario, no binarios versionados.
