# Roadmap de `audio-compressor`

Plan de la utilidad offline de compresión de audio y del decoder `AUZ2` del engine. El diseño estable está en [`AUDIO_COMPRESSION.md`](../../engine/architecture/AUDIO_COMPRESSION.md); la arquitectura de streaming existente está en [`AUDIO_STREAMING.md`](../../engine/architecture/AUDIO_STREAMING.md).

## Fases

### C0 — Contenedor y baseline

- **Entregado**: `AUZ2` v1, mono PCM8, chunks, PCM crudo y Delta+RLE, verificación round-trip en la utilidad host.
- **Entregado**: parser portable `eng/audio/auz2.hpp` y reutilización de `eng/audio/pcm_codec.hpp`.
- **Pendiente**: test HOST completo con varios chunks y vectores truncados/desbordados.

### C1 — Ingestión de PC

- WAV PCM mono de 8/16 bits y RAW PCM8: baseline implementado.
- WAV estéreo, AIFF, FLAC y Ogg mediante una capa de ingestión documentada.
- Extracción de audio de vídeo descargado usando `yt-dlp` + `ffmpeg` como comandos externos optativos.
- Normalización explícita de frecuencia, canal, signo y amplitud, con informe de cada conversión.

### C2 — Evaluación y búsqueda

- Informe de tamaño, ratio, MSE, RMS, SNR y pico de error por archivo y chunk.
- Parámetros declarativos en línea de comandos y perfiles reproducibles en JSON.
- Barrido de tamaño de chunk 256/512/1024/2048 y elección por coste/calidad.
- Corpus de SFX, voz, percusión, pads y música; resultados en `out/playground/audio-compressor/`.

### C3 — Codec con pérdida barato

- IMA ADPCM 4-bit como codec portable y decoder host de referencia.
- Modos por chunk: silencio, ADPCM, PCM8 y Delta+RLE.
- Escala inicial, predictor y cabecera de estado por chunk.
- Test de equivalencia entre decoder host y decoder Amiga.

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

- Integración con `PcmStream` y doble/triple buffer de `AUDIO_STREAMING.md`.
- C++23 freestanding como implementación de referencia.
- ASM 68000 para bitstream ADPCM, integración predictiva y síntesis armónica después de medir.
- Vectores byte a byte, prueba de límites y `asm-audit.mjs` para cada rutina optimizada.
- Demo Amiga de reproducción desde RAM y posteriormente desde `trackdisk`.

## Criterios de aceptación

- Todo archivo generado por la utilidad se puede validar sin depender de una ruta absoluta ni de herramientas no declaradas.
- El decoder rechaza magic, versión, codec, payload y tamaños inválidos sin escribir fuera del destino.
- Cada codec nuevo tiene un encoder de referencia, decoder host, vector determinista y test Amiga cuando se use en hardware.
- Las afirmaciones de ratio y calidad proceden de un informe reproducible sobre un corpus identificado.
