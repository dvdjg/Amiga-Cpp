# Roadmap del pipeline AUZX

Plan del pipeline offline de compresión de audio y del decoder `AUZX` del engine. El diseño estable está en [`AUDIO_COMPRESSION.md`](../../engine/architecture/AUDIO_COMPRESSION.md); la arquitectura de streaming está en [`AUDIO_STREAMING.md`](../../engine/architecture/AUDIO_STREAMING.md).

## Fases

### C0 — Contenedor y baseline

- **Entregado**: `AUZX` v1, mono PCM8, índice de chunks, checksum opcional y parseo validado por HOST-360.
- **Entregado**: `host-tools/pack-pcm` reutiliza los codecs del engine y verifica el round-trip.
- **Entregado**: `eng/audio/media.hpp` centraliza PCM/AUZX y `PcmStream::seek` usa el índice.

### C1 — Ingestión de PC

- RAW PCM8 firmado: implementado en `pack-pcm` y `tools/audio/pack-auzx.mjs`.
- Conversión WAV PCM mono de 8/16 bits: pendiente en la capa de ingestión del pipeline.
- WAV estéreo, AIFF, FLAC y Ogg mediante una capa de ingestión documentada.
- Extracción de audio de vídeo descargado usando `yt-dlp` + `ffmpeg` como comandos externos optativos.
- Normalización explícita de frecuencia, canal, signo y amplitud, con informe de cada conversión.

### C2 — Evaluación y búsqueda

- Informe de tamaño, ratio, MSE, RMS, SNR y pico de error por archivo y chunk.
- Parámetros declarativos en línea de comandos y perfiles reproducibles en JSON.
- Barrido de tamaño de chunk 256/512/1024/2048 y elección por coste/calidad.
- Corpus de SFX, voz, percusión, pads y música; resultados en `out/playground/audio-compressor/`.

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

## Criterios de aceptación

- Todo archivo generado por la utilidad se puede validar sin depender de una ruta absoluta ni de herramientas no declaradas.
- El decoder rechaza magic, versión, codec, payload y tamaños inválidos sin escribir fuera del destino.
- Cada codec nuevo tiene un encoder de referencia, decoder host, vector determinista y test Amiga cuando se use en hardware.
- Las afirmaciones de ratio y calidad proceden de un informe reproducible sobre un corpus identificado.
