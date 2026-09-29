# Diseño de audio comprimido para Amiga

`audio-compressor` es la utilidad offline que convierte fuentes de audio de PC en un contenedor `AUZ2` reproducible por el engine en Amiga. La utilidad corre en PC y puede usar librerías del sistema para leer formatos de entrada en fases futuras, pero el formato de salida, el análisis de calidad y los codecs compartidos pertenecen al proyecto.

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
          └──────────────► encoder AUZ2 ──► archivo para Amiga
                                             │
                         índice/chunk ──────┤
                                             ▼
                 parser freestanding + decoder C++23/ASM
                                             │
                                             ▼
                                      PCM8 en Chip RAM → Paula
```

El binario host vive en `tools/audio-compressor/` porque reutiliza headers del engine. El vocabulario portable vive en `engine/include/eng/audio/`: `auz2.hpp` define el contenedor y reutiliza `pcm_codec.hpp` para Delta+RLE y ZX0. Las rutinas críticas de reproducción se podrán sustituir por ASM 68000 sin cambiar el formato ni la API de dominio.

## Contenedor AUZ2

Todos los enteros se escriben little-endian para que el parser sea explícito y estable entre PC y 68000.

```text
Cabecera fija, 28 bytes
  0..3    magic "AUZ2"
  4..5    versión = 1
  6..7    tamaño de cabecera = 24
  8..11   frecuencia de muestreo
  12      canales = 1
  13      bits = 8
  14..15  reservado
  16..19  muestras PCM totales
  20..21  muestras por chunk
  22..23  número de chunks
  24..27  reservado/checksum futuro

Por chunk, 8 bytes + payload
  0..1    muestras reconstruidas
  2..3    bytes comprimidos
  4       codec: 0 ZX0, 2 Delta+RLE, 3 PCM crudo
  5..7    reservado
  8..     payload del codec
```

El descriptor contiene el tamaño comprimido, por lo que el decoder puede saltar al siguiente chunk sin conocer el algoritmo. El tamaño reconstruido limita el destino y evita escribir fuera del buffer de Chip RAM. `AUZ2` usa mono PCM8 en la primera versión; estéreo y profundidades adicionales requieren una versión de formato explícita.

## Modos de compresión

La selección se hace por chunk, comparando tamaño y calidad:

| Modo | Estado | Uso previsto |
|---|---|---|
| PCM crudo | implementado | fallback y referencia de calidad |
| Delta+RLE | implementado | codec portable rápido, reutiliza `pcm_codec` |
| ZX0 | decoder engine existente | ratio alto cuando el encoder host se integre |
| IMA ADPCM 4-bit | planificado | modo principal con pérdida controlada |
| Delta/ADPCM cuantizado 3–6 bit | planificado | bitrate ajustable por chunk |
| Residual armónico | planificado | tonos estables: fundamental + hasta dos armónicos |
| Silence/hold/RLE largo | planificado | tramos sin energía o repetidos |

El análisis armónico no se almacena como metadato decorativo: solo se selecciona si sintetizar el tono, codificar el residual y guardar sus parámetros produce un coste total menor o una mejora de calidad justificada. El decoder leerá siempre el modo desde la cabecera del chunk.

## Análisis y error

El encoder debe conservar la señal normalizada como referencia y verificar el round-trip. Las métricas mínimas son MSE, RMS del error, SNR, pico absoluto y ratio `bytes_AUZ2 / bytes_PCM`. Para el material musical se añadirá ponderación perceptual por energía y bandas, sin usar una métrica perceptual para ocultar un error de reconstrucción byte a byte en modos lossless.

Los parámetros explorables serán frecuencia, canales de entrada, tasa de salida, tamaño de chunk, predictor, bits de cuantización, escala, dithering, noise shaping, preénfasis, codec y umbrales de tonalidad. Cada ensayo debe guardar la configuración completa y el hash de la entrada bajo `out/playground/audio-compressor/`.

## Restricciones del decoder Amiga

- La IRQ de audio solo cambia buffers y marca trabajo pendiente; la descompresión se realiza en el bucle o en una tarea cooperativa.
- El payload puede residir en Fast RAM, pero el PCM que lee Paula debe terminar en Chip RAM.
- El decoder C++23 será la referencia portable; los bucles medidos se podrán portar a ASM 68000 después de disponer de un test de equivalencia byte a byte y una auditoría `asm-audit.mjs`.
- La síntesis armónica usará tabla de onda y acumulador de fase; el objetivo A500 inicial es fundamental más un armónico, con un máximo de dos armónicos en la primera implementación.

## Fuentes externas y licencias

ZX0 se integra mediante el decoder ya portado en `eng/audio/zx0.hpp`, atribuido a Einar Saukas. Para IMA ADPCM se estudiará `Kalmalyzer/adpcm-68k` como referencia de implementación 68000, verificando licencia y comportamiento antes de incorporar código. `ffmpeg` y `yt-dlp` serán dependencias opcionales del entorno del usuario, no binarios versionados.
