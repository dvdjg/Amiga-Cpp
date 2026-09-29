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

### C12 — ACP1: diccionario y reproducción estructural

- Diseñar `ACP1` como contenedor superior a AUZX para diccionario de unidades, pistas y eventos.
- Implementar encoder C++23 con HPSS, división multibanda, firmas espectrales y detección de unidades repetidas o similares.
- Deduplicar unidades y permitir payload AUZX, Delta+ZX0, Fibonacci, IMA, ADPCM cuantizado o residual armónico.
- Asignar pistas a Paula 0..2 o mixer 0..3, conservando destino, pitch y transiciones en eventos validados.
- Implementar parser Amiga C++23 con `ByteReader`/`Span`, offsets validados, capacidad fija y sin STL/heap durante reproducción.
- Medir y portar a ASM 68000 solo los núcleos de decode, crossfade y síntesis con equivalencia byte a byte.
- Tests previstos: HOST-381 parser, HOST-382 deduplicación, HOST-383 secuenciador, HOST-384 selección Paula/mixer y demo híbrida.

### C13 — Corpus y ajuste estructural

- Comparar AUZX lineal frente a ACP1 incluyendo diccionario, eventos, tablas y fades.
- Elegir HPSS, tamaño de unidad y umbral de similitud con coste conjunto de error, RAM, voces y CPU.
- Generar informes reproducibles con hash de entrada, configuración, versión del encoder y destino de cada pista.
- Preservar los pseudocódigos de HPSS, mediana deslizante, barrera de tareas y player como contratos de implementación en `AUDIO_COMPRESSION.md`.

### C14 — API unificada de reproducción y cues

- Diseñar `PlaybackHandle` generacional común para sample, stream y música ACP1.
- Controlar pausa, reanudación, parada y volumen por handle sin exponer canales ni voces.
- Añadir `AudioCue` en ACP1 y `MsgType::AudioCue` en el mini-SO con handle, código, valor y posición.
- Adaptar `AudioSystem`/`GameAudio` para devolver handles reales y conectar la tabla de sesiones con Paula, mixer, `PcmStream` y caché ACP1.

### C15 — Utility única de línea de órdenes

- Crear `host-tools/audio-compressor/audio-compressor` en C++23 como única aplicación pública; integrar dentro sus módulos de ingestión, codecs, AUZX y ACP1. No habrá dos ejecutables que el usuario deba combinar.
- Implementar arrastrar/soltar: un único archivo sin opciones usa defaults, genera `.auzx` para samples y `.acp1` para música, sin sobrescribir entradas.
- Implementar CLI explícita con `--mode`, `--config`, `--out`, `--codec`, `--sample-rate`, `--chunk`, `--ram-budget`, `--window`, `--hpss`, `--bands`, `--threads`, `--report` y `--force`.
- Implementar configuración con precedencia `defaults < config < CLI` y volcado de configuración resuelta.
- Implementar clasificación `auto` por duración, energía, onsets, repetición y coste; permitir `sample`/`music` forzado.
- Implementar pipeline sample: ingestión, candidatos, round-trip, métricas, AUZX e informe.
- Implementar pipeline música: lectura multipista/stems, HPSS, bandas, deduplicación, unidades, destinos Paula/mixer, eventos ACP1 e informe comparativo AUZX/ACP1.
- Test de aceptación: arrastrar WAV corto, arrastrar WAV largo, forzar ambos modos, config externa, salida existente y error de formato.

### C16 — Operación y corpus

- Registrar el corpus FreePD archivado y otros corpus disponibles sin incluir media en Git.
- Añadir `--list-codecs`, `--dump-config` y `--dry-run` para inspeccionar decisiones sin escribir binarios.
- Generar informes JSON y resumen legible con hash de entrada, configuración, clasificación, unidades, tracks, destino y métricas.
- Verificar que cualquier ejecución completa produce solo salidas bajo `out/` salvo el archivo destino solicitado explícitamente.

### C17 — Candidatas y selección automática

- Generar en una ejecución candidatas lineales, ADPCM, cuantizadas, ACP1 estructurales y ACP1 híbridas.
- Comparar cada candidata sobre la mezcla final y, cuando exista, sobre cada stem.
- Incluir en la función de coste tamaño total, MSE/RMS/SNR/pico, RAM Chip/Fast, número de unidades/eventos, voces requeridas y coste de CPU estimado.
- Seleccionar una salida principal y conservar opcionalmente todas las candidatas bajo `out/playground/audio-compressor/<run>/candidates/`.
- Añadir `--keep-candidates`, `--candidate-set` y `--stems all|mono|lista`.

### C18 — Importación multipista

- Leer WAV multicanal preservando stems antes del downmix.
- Importar módulos/tracker y conservar canales, instrumentos y patrones como pistas lógicas cuando el formato lo permita.
- Probar repetición por stem y por mezcla completa; rechazar una separación si empeora tamaño/calidad.
- Validar que ACP1 reproduce la misma duración y sincronía entre tracks.

## Criterios de aceptación

- Todo archivo generado por la utilidad se puede validar sin depender de una ruta absoluta ni de herramientas no declaradas.
- El decoder rechaza magic, versión, codec, payload y tamaños inválidos sin escribir fuera del destino.
- Cada codec nuevo tiene un encoder de referencia, decoder host, vector determinista y test Amiga cuando se use en hardware.
- Las afirmaciones de ratio y calidad proceden de un informe reproducible sobre un corpus identificado.
