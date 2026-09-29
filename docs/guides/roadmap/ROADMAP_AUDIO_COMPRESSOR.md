# Roadmap del pipeline AUZX

Plan del pipeline offline de compresión de audio y del decoder `AUZX` del engine. El diseño estable está en [`AUDIO_COMPRESSION.md`](../../engine/architecture/AUDIO_COMPRESSION.md); la arquitectura de streaming está en [`AUDIO_STREAMING.md`](../../engine/architecture/AUDIO_STREAMING.md).

El corpus mixto previsto para ajustar los parámetros está descrito en [`AUDIO_CORPUS_FREEPD.md`](AUDIO_CORPUS_FREEPD.md). Incluye ambiente tonal, ritmo electrónico, piano y material experimental; las copias descargadas son salidas de `out/` y no forman parte del repositorio.

## Fases

### C0 — Contenedor y baseline

- **Entregado**: `AUZX` v1, mono PCM8, índice de chunks, checksum opcional y parseo validado por HOST-360.
- **Entregado**: `host-tools/pack-pcm` reutiliza los codecs del engine y verifica el round-trip.
- **Entregado**: `eng/audio/media.hpp` centraliza PCM/AUZX y `PcmStream::seek` usa el índice.

### C1 — Ingestión de PC

- RAW PCM8 firmado: implementado en `pack-pcm` y `tools/audio/pack-auzx.mjs`.
- Conversión WAV PCM mono/estéreo de 8/16 bits: implementada en `pack-pcm`, con downmix estéreo y
  frecuencia heredada del fichero salvo override explícito.
- WAV estéreo, AIFF, FLAC y Ogg mediante una capa de ingestión documentada.
- Extracción de audio de vídeo descargado usando `yt-dlp` + `ffmpeg` como comandos externos optativos.
- Normalización explícita de frecuencia, canal, signo y amplitud, con informe de cada conversión.

### C2 — Evaluación y búsqueda

- Informe de tamaño, ratio, MSE, RMS, SNR y pico de error por archivo y chunk.
- Parámetros declarativos en línea de comandos y perfiles reproducibles en JSON.
- Barrido de tamaño de chunk 256/512/1024/2048 y elección por coste/calidad.
- Corpus de SFX, voz, percusión, pads y música; resultados en `out/playground/audio-compressor/`.
- Evaluación por ventanas reutilizadas con presupuesto host explícito de 6 GiB; no se carga el audio completo por obligación.
- Adaptadores de codec separados para tamaño y reconstrucción; no se publica una métrica perceptual sin señal decodificada.

### C3 — Codec con pérdida barato

- Fibonacci Delta e IMA ADPCM 4-bit: implementados y cubiertos por HOST-357 y HOST-359.
- ASM 68000 y referencia C++: equivalencia cubierta por la demo `277_codec_equiv`.
- Modos por archivo/chunk: PCM, Delta+RLE, Delta+ZX0, Fibonacci, IMA, ZX0 y aPLib.

### C4 — Cuantización entrenable

- Lloyd-Max para 3–6 bits usando `eng::quick_sort` o una alternativa ya existente, nunca `qsort` en el camino Amiga.
- Tablas de reconstrucción y decisión exportables a un asset de engine.
- Ponderación por energía y evaluación frente a IMA estándar.
- Dithering y noise shaping solo en el encoder PC; el decoder conserva aritmética entera.

### C5 — Residual armónico

- FFT/Goertzel offline, HNR, estabilidad de frecuencia y reducción energética.
- Parámetros de fundamental, fase, amplitudes y envelope simple en chunks tonales.
- Síntesis por tabla de onda y residual ADPCM/Delta+RLE.
- Selección automática únicamente cuando el tamaño total o la calidad superen el modo normal.

### C6 — Decoder Amiga y rendimiento

- Integración con `PcmStream`, `AudioFeeder` y doble/triple buffer.
- C++23 freestanding como implementación de referencia y ASM 68000 para rutas críticas.
- Vectores byte a byte, prueba de límites y `asm-audit.mjs` para cada rutina optimizada.
- Demos `277_codec_equiv` y `278_stream_disk` como evidencia de equivalencia y streaming.

### C7 — Intención de reproducción continua

- **Entregado**: `StreamIntent` y `StreamExecutor` en `eng/audio/stream_intent.hpp`, validados por HOST-375.
- **Contrato**: el juego solicita un recurso y política de reproducción; el backend elige Paula DMA o mixer y conecta `PcmStream`/`AudioFeeder`.
- **Pendiente**: backend Amiga que resuelva `AudioStreamId` desde `media::Info`, asigne Chip buffers y publique `IntentDone` al completar o `AudioUnderrun` al fallar.

### C8 — Backend y planner de streams

- **Entregado**: `MediaStreamBackend` valida `media::Info` antes de delegar la reserva del stream.
- **Entregado**: `StreamingAudioPlanner` reutiliza `IntentQueue` y `IntentDone` sin mezclar la política de voces cortas de `SoundPlanner`.
- **Pendiente**: resolver recursos reales, asignar Chip RAM y conectar el feeder IRQ al backend Paula/mixer.

### C9 — Métricas y búsqueda automática

- **Entregado**: evaluación round-trip por ventanas con tamaño, error cuadrático, MSE escalado, SNR aproximada y pico de error.
- **Entregado**: búsqueda determinista del candidato con menor tamaño y desempate por error.
- **Pendiente**: adaptar los encoders reales AUZX y generar informes de corpus con configuración y hash de entrada.

### C10 — Parámetros de coma fija

- **Entregado**: `AudioTuning<S>` y `AudioErrorMetrics<S>` en `eng/audio/audio_tuning.hpp`.
- **Entregado**: HOST-379 instancia el algoritmo con `float` y `Fixed<s32,16>`.
- **Pendiente**: seleccionar el formato fixed definitivo por parámetro y comprobar codegen 68000 para el camino de reproducción.

### C11 — Tablas de cuantización

- **Entregado**: `eng::util::lloyd_max` como plantilla scalar-independent, con capacidad y scratch explícitos.
- **Entregado**: HOST-380 con `float` y `Fixed<s32,16>`.
- **Pendiente**: conectar la tabla entrenada al encoder IMA/residual y exportarla al formato AUZX.

## Criterios de aceptación

- Todo archivo generado por la utilidad se puede validar sin depender de una ruta absoluta ni de herramientas no declaradas.
- El decoder rechaza magic, versión, codec, payload y tamaños inválidos sin escribir fuera del destino.
- Cada codec nuevo tiene un encoder de referencia, decoder host, vector determinista y test Amiga cuando se use en hardware.
- Las afirmaciones de ratio y calidad proceden de un informe reproducible sobre un corpus identificado.
