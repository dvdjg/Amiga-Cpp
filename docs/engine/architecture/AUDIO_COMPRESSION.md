# Diseño de audio comprimido para Amiga

El pipeline de audio convierte fuentes de PC en PCM mono de 8 bits con signo y las empaqueta en `AUZX`, el contenedor que consume el engine Amiga. La única aplicación pública es `host-tools/audio-compressor/audio-compressor`; sus módulos internos reutilizan los codecs y parsers de `engine/include/eng/audio/`. Las herramientas históricas `pack-pcm` y `tools/audio/pack-auzx.mjs` quedan como implementaciones de transición y no forman parte del flujo de usuario final.

El pipeline tiene dos niveles de formato. `AUZX` representa una señal PCM lineal dividida en chunks; `ACP1` representa una obra estructurada como diccionario de unidades reutilizables y pistas de eventos. Una unidad `ACP1` puede contener un payload AUZX, ADPCM o residual, y una pista decide si se reproduce por Paula directa o por una voz del mixer.

## Objetivos

- Producir audio mono PCM8 firmado, compatible con Paula y con el streaming por chunks del engine.
- Probar configuraciones de codec, predicción, cuantización y tamaño de bloque sobre un corpus reproducible.
- Mantener el decoder Amiga freestanding, sin STL, excepciones ni heap en el camino de reproducción.
- Permitir que la utilidad lea WAV y RAW inicialmente, y delegar la extracción de audio de vídeo descargado a `ffmpeg`/`yt-dlp` sin incorporar sus binarios al repositorio.
- Comparar el resultado decodificado con la señal normalizada y registrar tamaño, ratio y métricas de error.
- Ofrecer una utilidad única de PC que funcione tanto por CLI como arrastrando un archivo sobre el ejecutable.
- Seleccionar automáticamente entre pipeline de sample y pipeline musical, permitiendo forzar cualquiera de los dos.

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

La aplicación única se organiza internamente en ingestión, análisis, codecs, serialización AUZX/ACP1, búsqueda de candidatas e informes. La primera vertical vive en `host-tools/audio-compressor/src/main.cpp` y puede generar AUZX; el resto de módulos se integrará dentro de la misma aplicación, no como ejecutables adicionales. Las salidas temporales viven en `out/tmp/audio-compressor/`, las conversiones finales en `out/assets/audio-compressor/converted/`, las candidatas en `out/assets/audio-compressor/candidates/` y los informes en `out/reports/audio-compressor/`. El contenedor portable está definido por `eng/audio/auzx.hpp`; `eng/audio/media.hpp` ofrece el punto único de reconocimiento y decodificación por chunk. Las rutinas críticas tienen referencia C++ y variantes ASM 68000 bajo el mismo contrato.

La utilidad incluye un reproductor host opcional basado en SDL3. `--play` permite escuchar la fuente normalizada mientras se ajustan codecs; SDL3 no es una dependencia del engine ni del formato generado y la conversión batch funciona aunque no esté instalada. Cuando está habilitado, SDL3 también proporciona la E/S host común de la aplicación; Win32 queda limitado a la distribución/enlace cuando el toolchain lo exige. El build preferido enlaza SDL3 estáticamente mediante `pkg-config --static` o `SDL3_ROOT`/`SDL3_DIR`; la salida no debe depender de `SDL3.dll`, aunque el sistema operativo puede cargar sus propios drivers de audio.

La utilidad orquestadora será `host-tools/audio-compressor/audio-compressor`. `pack-pcm` se conserva como herramienta de bajo nivel y `audio-compressor` compone ingestión, clasificación, análisis, selección, generación AUZX/ACP1 e informe.

## Interfaz de la utilidad

### Arrastrar y soltar

Si el ejecutable recibe exactamente un archivo de entrada y no recibe opciones, aplica defaults seguros y genera un archivo junto al original:

```text
audio-compressor tema.wav
  -> tema.acp1       si la clasificación es música

audio-compressor disparo.wav
  -> disparo.auzx    si la clasificación es sample
```

El nombre se obtiene sustituyendo la extensión; nunca se sobrescribe el original. Si el destino existe, se crea `name.1.acp1`, `name.2.acp1`, etc., salvo que se use `--force`.

### CLI explícita

```text
audio-compressor <entrada> [opciones]

--mode auto|sample|music       clasificación automática o forzada
--config <fichero>             JSON de configuración reproducible
--out <fichero|directorio>     destino AUZX/ACP1 o carpeta de trabajo
--codec auto|none|rle|fib|ima|zx0|delta-zx0|aplib
--sample-rate <Hz>             frecuencia objetivo; 0 conserva la fuente
--chunk <muestras>             chunk AUZX y unidad inicial ACP1
--ram-budget <bytes>           presupuesto host, por defecto 6442450944
--window <muestras>            ventana de análisis reutilizada
--hpss / --no-hpss             activar/desactivar separación estructural
--bands <lista>                bandas, por ejemplo 20:150,150:500,500:2000,2000:8000
--threads <N>                  paralelismo del encoder host
--report <fichero>             informe JSON con hash, decisiones y métricas
--force                        sobrescribir la salida explícitamente
--help                         mostrar ayuda y defaults
```

El archivo de configuración contiene las mismas claves que la CLI. La precedencia es `defaults < config < CLI`; el modo de arrastre usa solo defaults. Una configuración resuelta se copia al informe para que una ejecución pueda reproducirse sin depender del entorno del usuario. No existen dos programas que el usuario deba encadenar: la aplicación única llama internamente a sus módulos de ingestión, codec y escritura.

### Entrada multipista

La aplicación conserva los canales de una fuente multipista cuando el formato lo permite. En WAV multicanal, cada canal se ingiere como stem lógico antes del downmix opcional; en módulos tracker se importan patrones, instrumentos y canales como pistas lógicas; en contenedores multipista se preservan sus nombres y tasa común. El usuario puede seleccionar `--stems all`, una lista de stems o `--downmix mono`.

```text
fuente multipista
      │
      ├── stem 0: bajo/armónico ──┐
      ├── stem 1: percusión       ├─► firmas + repetición por stem
      ├── stem 2: armonía         │
      └── stem 3: residual ───────┘
                         │
              candidatas lineal / ACP1 / híbrida
```

Los stems ayudan a encontrar secuencias repetitivas que quedarían ocultas al mezclar primero. El encoder compara también una candidata de mezcla completa porque la separación puede introducir sangrado o aumentar el coste de eventos.

### Clasificación automática

`--mode auto` no decide solo por tamaño de archivo. La decisión usa duración, tasa, número de muestras, energía, onsets, repetición, estabilidad espectral y coste estimado:

```text
si --mode está forzado:
    usar el modo solicitado
si duración <= sample_max_duration y no hay estructura repetida:
    SAMPLE
si hay onsets/tempo, repetición de unidades o duración > music_min_duration:
    MUSIC
en otro caso:
    probar ambos pipelines sobre ventanas representativas
    elegir el menor coste con calidad admisible
```

Los umbrales (`sample_max_duration`, `music_min_duration`, repetición mínima y coste) forman parte de la configuración. El tamaño bruto nunca es el único criterio: un fichero corto puede ser música y uno largo puede ser un único sample de ambiente.

### Pipeline SAMPLE

```text
ingest WAV/RAW/ffmpeg
normalizar mono PCM8 y sample rate
probar candidatos de codec/chunk/quantización por ventanas
codificar y reconstruir cada chunk elegido
calcular MSE/RMS/SNR/pico/ratio
escribir AUZX
escribir informe y hash de entrada
```

### Pipeline MUSIC

```text
ingest y normalizar a la tasa objetivo
HPSS -> harmónico/percusivo/residual
dividir opcionalmente por bandas
detectar onsets, beats y ventanas candidatas
extraer firmas y deduplicar unidades
para cada unidad:
    probar codecs y parámetros por round-trip
    clasificar destino Paula/mixer
    crear TrackHeader, TrackEvent, envolventes y AudioCue
escribir tablas + diccionario + tracks como ACP1
escribir informe con ahorro frente a AUZX lineal
```

El pipeline de música puede caer a AUZX si el diccionario y los eventos ocupan más que la codificación lineal o si la calidad de concatenación no cumple el umbral. La elección queda registrada en el informe, no se oculta en el archivo.

### Candidatas comparables

La aplicación puede generar varias salidas de prueba en una sola ejecución. Cada candidata tiene un id y una configuración completa:

```text
candidate linear-rle:
    AUZX + DeltaRLE, chunk 4096
candidate linear-ima:
    AUZX + IMA, chunk 2048, cuantización configurada
candidate structured:
    ACP1, HPSS, unidades 1024, Paula/mixer automático
candidate structured-wide:
    ACP1, bandas 4, unidades 2048, crossfade equal-power

para cada candidata:
    codificar por ventanas/stems
    reconstruir la señal o la mezcla final
    medir bytes totales, MSE, RMS, SNR, pico, RAM, unidades, eventos y coste de reproducción
seleccionar la mejor según la función de coste configurada
guardar todas las candidatas si se usa --keep-candidates
```

El tamaño ganador incluye cabecera, tablas, diccionario, eventos, fades y cualquier buffer requerido; no se permite comparar solo payloads comprimidos.

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

## Contenedor estructural ACP1

`ACP1` representa una obra como pistas temporales, eventos, unidades reutilizables y segmentos comprimidos. Este documento distingue los layouts v1/v2 que hoy parsea el engine de la propuesta normativa v3 para la combinatoria completa. El Amiga no analiza HPSS ni busca repeticiones: valida el archivo, planifica recursos y prepara PCM mediante trabajo cooperativo.

```text
┌──────────────────────────────────────────────────────────────┐
│ ACP1 header: tasa, flags, límites y offsets                  │
├──────────────────────────────────────────────────────────────┤
│ tablas globales: cuantización, alpha, ondas y parámetros     │
├──────────────────────────────────────────────────────────────┤
│ diccionario: UnitHeader + payload AUZX/ADPCM/residual        │
├──────────────────────────────────────────────────────────────┤
│ tracks: Paula 0..2 o mixer 0..3                              │
│         TrackEvent {unit, inicio, duración, volumen, fade}  │
└──────────────────────────────────────────────────────────────┘
```

El layout binario se escribe con lectores/escritores little-endian con límites; no se usa `#pragma pack` ni `reinterpret_cast` como API de parseo. El parser valida versiones, secciones, offsets, límites de payloads, destino de pista y referencias de unidad antes de reproducir.

### Layout ACP1 v1/v2 actualmente implementado

El layout binario de 40 bytes descrito inmediatamente abajo es el formato real de ACP1 v1/v2 que el parser implementa. V1 tiene un evento por track; v2 admite secuencias no solapadas. Este layout solo embebe unidades AUZX mono PCM8: no representa los catálogos multi-codec, las envolventes ni los cues de la propuesta v3 posterior. Esta distinción es normativa: una implementación v1/v2 debe rechazar v3 por versión desconocida.

Los bloques de audio de una pista pueden reutilizarse si coinciden byte a byte. La deduplicación aproximada requiere firmas perceptuales, comprobación de fase y evaluación de calidad y permanece desactivada. En `Rondo_alla_turca.ogg`, el resultado medido fue mayor que AUZX lineal tanto en tamaño como en MSE; HPSS aumentó esos dos costes todavía más, por lo que ninguna de esas opciones se selecciona automáticamente.

Todos los enteros son little-endian y los offsets son absolutos desde el inicio del archivo. Las tablas tienen orden fijo: cabecera, unidades, payloads de unidades, tracks y eventos. El tamaño total debe coincidir exactamente con el archivo.

| Offset | Tamaño | Campo | Regla v1/v2 |
|---:|---:|---|---|
| 0 | 4 | magic | ASCII `ACP1` |
| 4 | 2 | version | `1` o `2` |
| 6 | 2 | flags | `0` |
| 8 | 4 | sample_rate | 1..65535 Hz |
| 12 | 2 | unit_count | 1..65535; v1: no mayor que `track_count`; v2: número de unidades deduplicadas |
| 14 | 1 | track_count | 1..7 |
| 15 | 1 | reserved | `0` |
| 16 | 4 | total_samples | Extensión de la timeline común; mayor que cero |
| 20 | 4 | tables_offset | `0` en esta versión |
| 24 | 4 | units_offset | Debe ser `40` |
| 28 | 4 | tracks_offset | Inicio de tabla tras payloads |
| 32 | 4 | events_offset | Inicio de tabla de eventos |
| 36 | 4 | file_size | Igual al tamaño del archivo |

Los offsets v1/v2 son absolutos y las regiones son contiguas: cabecera, UnitHeaders, payloads AUZX, TrackHeaders y eventos. `decoded_samples` de una unidad es el total exacto declarado por su AUZX embebido; `total_samples` es la extensión de los eventos de la obra. V2 permite que sean distintos porque una unidad corta puede repetirse en varias posiciones.

La cabecera mide 40 bytes. Cada `UnitHeader` mide 24 bytes: `id:u32`, `payload_offset:u32`, `payload_size:u32`, `decoded_samples:u32`, `codec:u8`, `flags:u8`, `gain:u8`, `reserved:u8`, `phase:u16`, `reserved2:u16`. Los IDs son consecutivos desde cero, `codec=1` identifica un AUZX completo, los flags y reservas son cero, y `gain=255`. `decoded_samples` es la duración reconstruida del payload y puede ser menor que la timeline total cuando la unidad se reutiliza en ACP1 v2.

Cada `TrackHeader` mide 8 bytes: `destination:u8`, `flags:u8`, `event_count:u16`, `events_offset:u32`. Los destinos 0..2 reservan Paula 0..2 y 3..6 reservan voces mixer 0..3; ambas versiones exigen destinos únicos, `flags=0` y un offset contiguo a la secuencia de eventos de la pista. V1 exige exactamente un evento por pista; v2 permite uno o más.

Cada `TrackEvent` mide 20 bytes: `unit_id:u32`, `start_sample:u32`, `duration:u32`, `gain:u8`, `pitch:s8`, `fade_in:u16`, `fade_out:u16`, `flags:u16`. V1 exige `unit_id` válido, inicio cero, duración igual a `total_samples`, `gain=255`, `pitch=0`, fades y flags cero. V2 permite ganancia de evento 0..255 y exige inicio no anterior al fin del evento previo, duración no nula y rango dentro de `total_samples` y `decoded_samples` de la unidad; pitch, fades y flags deben ser cero. Los huecos entre eventos son silencio. Varias pistas/eventos pueden referirse a una misma unidad deduplicada.

El parser rechaza rangos que desbordan el archivo, offsets de tabla incoherentes, payloads AUZX inválidos, IDs no consecutivos, destinos repetidos o fuera de rango y eventos incompatibles con la versión declarada. El payload AUZX y cada rango del índice se validan con `eng::audio::auzx`; el parser ACP1 solo devuelve vistas y no reserva memoria. `media` decodifica ventanas por evento; `Acp1Stream` mezcla cooperativamente a buffers PCM y la IRQ solo avanza o cambia el buffer.

## Propuesta de formato completo ACP1 v3 (no implementada)

Este diseño es la propuesta normativa para contener la combinatoria de códecs, representaciones, automatización y eventos de una obra. No está implementado ni debe confundirse con el subconjunto v1/v2 anterior: el parser actual acepta v1/v2 y rechaza v3. La compatibilidad se conserva manteniendo magic `ACP1` y versionando el layout; un lector que no soporte v3 debe fallar con `UnsupportedVersion` antes de reservar o tocar Paula. Los tamaños y referencias de esta propuesta deben implementarse y probarse antes de publicar un encoder v3.

### Modelo y unidades de tiempo

ACP1 v3 separa cinco conceptos: representación de una unidad, compresión de cada segmento, referencia temporal de un evento, automatización de controles y salida física elegida por el planificador. La única unidad de reloj persistida es la muestra de la timeline maestra. `start_sample`, `duration`, puntos de envolvente y cues se expresan en muestras a `sample_rate`; el renderer traduce ese reloj a chunks PCM y el backend a periodos Paula. Los índices de tabla y tamaños de payload no son tiempo.

```text
obra (timeline en muestras)
├── pista 0: evento -> unidad -> segmentos codec A/B -> decoder ─┐
│              envolvente de ganancia/pitch                    ├-> planificador -> salida
├── pista 1: eventos + cues                                     │    ├─ Paula directa 1..3
├── pista N: eventos + cues                                     │    └─ Mixer AUD0 (4 voces SW)
└── cues globales                                                ┘
```

La secuencia de un track es ordenada por `start_sample`. Los eventos no se solapan dentro del mismo track en v3; tracks distintos sí pueden solaparse. Un hueco es silencio. Los loops se expresan como intervalo de eventos del track y repiten la secuencia sin alterar los tiempos originales del archivo. Los cues son marcadores puntuales independientes de los eventos audibles y se despachan una sola vez por cruce temporal, salvo repetición explícita de la región de loop.

### Cabecera y directorio de secciones

Todos los enteros son little-endian. El archivo empieza con una cabecera fija de 64 bytes; a continuación hay un directorio de secciones con entradas de 16 bytes. Cada offset es absoluto desde el primer byte del archivo. Las tablas ocupan `entry_count * entry_size`; las regiones de bytes usan `entry_size=1` y `entry_count` como longitud. Las secciones se ordenan por `section_id`, están alineadas a 4 bytes, no se solapan y deben terminar antes de `file_size`.

| Offset | Tamaño | Campo | Regla ACP1 v3 |
|---:|---:|---|---|
| 0 | 4 | magic | ASCII `ACP1` |
| 4 | 2 | major_version | `3` |
| 6 | 2 | minor_version | `0` para esta propuesta |
| 8 | 2 | header_size | `64` |
| 10 | 2 | flags | `0` en v3.0; cualquier bit no definido implica rechazo |
| 12 | 4 | sample_rate | Frecuencia maestra, 1..65535 Hz |
| 16 | 8 | timeline_samples | Extensión temporal de la obra, mayor que cero |
| 24 | 2 | section_count | Exactamente 17 en v3.0 |
| 26 | 1 | track_limit | Concurrencia lógica declarada, 1..7 |
| 27 | 1 | required_paula_voices | Voces Paula directas mínimas, 0..3 |
| 28 | 1 | required_mixer_voices | Voces software Mixer mínimas, 0..4 |
| 29 | 1 | default_route | `0=Auto`, `1=PreferPaula`, `2=PreferMixer` |
| 30 | 2 | reserved | Cero |
| 32 | 4 | section_directory_offset | Debe ser `64` |
| 36 | 4 | file_size | Tamaño total exacto, 1..`0xffffffff` bytes |
| 40 | 2 | checksum_algorithm | `0` sin checksum; `1` CRC-32 con el campo `checksum` a cero |
| 42 | 2 | reserved | Cero |
| 44 | 4 | checksum | Cero si el algoritmo es `0` |
| 48 | 2 | master_gain_q8_8 | Ganancia maestra inicial; `256` = unidad |
| 50 | 14 | reserved | Cero en v3.0 |

Cada entrada `SectionEntry` mide 16 bytes: `section_id:u16`, `flags:u16`, `entry_size:u32`, `entry_count:u32`, `offset:u32`. `flags bit 0 = REQUIRED`; los demás bits deben ser cero en v3.0. Cada ID aparece una vez; v3.0 requiere secciones 1..5 y permite 6..17. Secciones desconocidas solo se pueden saltar si son opcionales y su rango cabe íntegramente en el archivo; una sección desconocida requerida produce `UnsupportedSection`.

| ID | Sección | Tamaño de entrada | Requerida | Contenido |
|---:|---|---:|---|---|
| 1 | `Units` | 24 | Sí | Representación, duración y rango de segmentos |
| 2 | `Segments` | 28 | Sí | Codec y límites de cada bloque de salida |
| 3 | `PayloadBytes` | 1 | Sí | Bytes comprimidos referenciados por `payload_offset` absoluto |
| 4 | `Tracks` | 40 | Sí | Ruta, eventos, ganancia, paneo y loop |
| 5 | `Events` | 40 | Sí | Unidad, tiempo, pitch, gain, envolventes y cues |
| 6 | `Envelopes` | 16 | No | Automatización reutilizable |
| 7 | `EnvelopePoints` | 12 | No | Puntos temporales y valores fixed-point |
| 8 | `Cues` | 32 | No | Sucesos puntuales de gameplay/sincronización |
| 9 | `SynthesisParams` | 28 | No | Parámetros de unidad aditiva/híbrida |
| 10 | `Partials` | 8 | No | Parciales armónicos de síntesis |
| 11 | `CodecParams` | 16 | No | Parámetros versionados por segmento |
| 12 | `Strings` | 1 | No | Nombres UTF-8 de obra y pistas |
| 13 | `Codebooks` | 16 | No | Codebooks de PCM cuantizado |
| 14 | `CodebookValues` | 1 | No | Valores PCM8 firmados de codebooks |
| 15 | `WaveTables` | 16 | No | Descriptores de wavetables custom |
| 16 | `WaveBytes` | 1 | No | Datos PCM8 firmados de wavetables |
| 17 | `CuePayloadBytes` | 1 | No | Payload opaco limitado para cues |

Las tablas fijas usan el `entry_size` de la matriz; las regiones `PayloadBytes`, `Strings`, `CodebookValues`, `WaveBytes` y `CuePayloadBytes` usan `entry_size=1`. Las 17 secciones conocidas aparecen una vez; las opcionales vacías conservan su entrada con `entry_count=0`. Las regiones se alinean a 4 bytes, no se solapan y el padding es cero. Segmentos referencian `PayloadBytes` por offset absoluto; nombres, codebooks, wavetables y datos de cue usan offsets relativos a su sección. Los eventos de cada pista se almacenan contiguos y ordenados; cues del mismo instante preservan el orden de tabla.

### Unidades, segmentos y códecs

Un `Unit` es una fuente lógica PCM mono reconstruible y reutilizable. `representation:u8` toma `0=PCM`, `1=Additive`, `2=Hybrid`; flags restantes son cero en v3.0. Layout `Unit` de 24 bytes:

| Campo | Tipo | Semántica |
|---|---|---|
| `id` | u32 | Índice de la unidad, consecutivo desde cero |
| `representation` | u8 | PCM, modelo aditivo o modelo+residual |
| `flags` | u8 | `0` en v3.0 |
| `decoded_samples` | u32 | Longitud PCM reconstruida de la unidad |
| `first_segment` | u32 | Primer segmento contiguo de su payload |
| `segment_count` | u16 | Número de segmentos/componentes de datos |
| `synthesis_index` | u16 | Índice en `SynthesisParams` o `0xffff` si no aplica |
| `reference_gain_q8_8` | u16 | Ganancia de referencia, `256` = unidad sin escalar |
| `reserved` | u16 | Cero |
| `reserved2` | u16 | Cero; completa el tamaño fijo de 24 bytes |

Layout `Segment` de 28 bytes: `unit_id:u32`, `sample_start:u32`, `decoded_samples:u32`, `payload_offset:u32`, `payload_size:u32`, `codec:u16`, `role:u8`, `flags:u8`, `codec_params_index:u16`, `reserved:u16`. El payload es exactamente `PayloadBytes[payload_offset..payload_offset+payload_size]`, sin cabecera implícita ni padding dentro de `payload_size`. Los segmentos PCM principales particionan `[0, Unit.decoded_samples)` sin huecos ni solapamientos; segmentos residuales pueden solaparse temporalmente con PCM u otros roles y se suman antes de saturar PCM8. `role`: `0=PCM principal`, `1=residual armónico`, `2=residual percusivo`, `3=ruido/ambiente`. Cada segmento declara su códec, incluso dentro de una misma unidad.

Los IDs de códec son estables dentro de major version:

| ID | Códec | Transformación antes/después del codec | Uso / restricciones |
|---:|---|---|---|
| 0 | PCM8 | Ninguna | Referencia lossless y fallback |
| 1 | DeltaRLE | Diferenciación + ByteRun1 | Lossless; bloques cortos/planos |
| 2 | ZX0 | Ninguna | Lossless; payload auto-terminado y límites validados |
| 3 | DeltaZX0 | Diferenciación + ZX0 | Lossless; estado delta reiniciado en cada segmento |
| 4 | APLib | Ninguna | Lossless; depacker debe recibir salida limitada |
| 5 | FibonacciDelta | Predictor IFF Fibonacci | Lossy 4-bit; codec parameters identifican seed/regla |
| 6 | IMA ADPCM | Predictor IMA/DVI | Lossy; predictor/índice inicial por segmento |
| 7 | QuantizedPCM | Cuantizador codebook | Lossy; índice obligatorio de tabla en `CodecParams` |
| 8.. | Extensión | Registrado por minor/major futuro | Desconocido requerido implica rechazo |

No se considera AUZX como códec de segmento: AUZX es un contenedor lineal completo. ACP1 v3 almacena la compresión directamente en cada `Segment`, evitando contenedor anidado y permitiendo elegir códec distinto incluso dentro de una unidad. Un importador puede aceptar AUZX como fuente, pero al empaquetar extrae sus bloques y vuelve a serializarlos como segmentos ACP1.

`CodecParams` mide 16 bytes: `codec:u16`, `params_version:u16`, `block_samples:u16`, `predictor:u8`, `quantizer:u8`, `param0:u32`, `param1:u32`. `block_samples=0` no impone granularidad adicional; en otro caso el segmento respeta esa granularidad salvo el último bloque de la unidad. Cada códec define sus unidades/rangos y valores admitidos; el codec del registro coincide con el del segmento. Parámetros requeridos desconocidos implican rechazo.

`Codebook` mide 16 bytes: `id:u16`, `bits_per_index:u8`, `flags:u8`, `first_value:u32`, `value_count:u16`, `reserved:u16`, `reserved2:u32`. Los valores PCM8 firmados están en `CodebookValues`; `value_count` está entre 1 y `2^bits_per_index`, y los valores deben ser estrictamente crecientes. `WaveTable` mide 16 bytes: `id:u16`, `sample_count:u16`, `first_byte:u32`, `byte_count:u32`, `reserved:u32`; `sample_count` es potencia de dos entre 16 y 1024 y `byte_count` es igual a `sample_count`.

### Tracks y eventos

Layout `Track` de 40 bytes: `id:u16`, `route:u8`, `flags:u8`, `first_event:u32`, `event_count:u32`, `gain_envelope:u16`, `pitch_envelope:u16`, `gain_q8_8:u16`, `pan_s8:u8`, `priority:u8`, `loop_first_event:u32`, `loop_end_event:u32`, `name_offset:u32`, `name_length:u16`, `reserved:u16`, `reserved2:u16`.

`route` toma `0=Auto`, `1=PreferPaula`, `2=PreferMixer`, `3=PaulaRequired`, `4=MixerRequired`. `flags bit 0=loop enabled`; loop interval es `[loop_first_event, loop_end_event)` y debe quedar dentro de la tabla del track. `gain_q8_8` usa `256` como unidad y `pan_s8` va de -127 (izquierda) a +127 (derecha); el mixer/amiga puede aproximar paneo por ruteo de canales y el planner lo declara en su resultado.

Layout `Event` de 40 bytes: `track_id:u16`, `flags:u16`, `start_sample:u64`, `duration:u32`, `unit_id:u32`, `unit_offset:u32`, `gain_q8_8:u16`, `pitch_semitones_q8_8:s16`, `gain_envelope:u16`, `pitch_envelope:u16`, `cue_first:u32`, `cue_count:u16`, `reserved:u16`. `unit_offset` selecciona una subregión de la unidad; `duration` no puede exceder sus muestras desde ese offset. Los eventos del mismo track no se solapan. `flags bit 0=one-shot cue boundary`; los demás bits son cero en v3.0. `gain_envelope` y `pitch_envelope` admiten `0xffff` para “sin envolvente”. `cue_first/cue_count` forman un rango dentro de `Cues`; sus cues deben estar dentro de `[start_sample, start_sample+duration)` o ser marcadores terminales con `flags bit 0`.

`pitch_semitones_q8_8` admite desde -128 hasta casi +128 semitonos, con `0` como tono original. `flags bit 0` indica una frontera de cue one-shot; bits 1..2 seleccionan `0=backend default`, `1=preserve timeline by resampling`, `2=Paula period shift changes duration`, `3=invalid`; bits superiores son cero. En Paula directa se cambia `AUDxPER` en límites de evento/segmento. El periodo cambia el tiempo de salida; para preservar pitch y timeline se requiere remuestreo cooperativo, y la opción 2 declara duración audible variable. El Mixer Photon actual no tiene pitch por voz: el backend remuestrea el bloque cooperativamente o rechaza el evento si excede scratch/tiempo. No se escribe `AUDxPER` desde la IRQ del Mixer.

### Envolventes

Una envolvente se comparte entre tracks y eventos. Layout `Envelope` de 16 bytes: `target:u8`, `interpolation:u8`, `flags:u16`, `first_point:u32`, `point_count:u16`, `loop_start_point:u16`, `loop_end_point:u16`, `reserved:u16`.

`target`: `0=track gain`, `1=event gain`, `2=pitch semitones`, `3=pan`; otros targets se rechazan en v3.0. `interpolation`: `0=step`, `1=linear`, `2=equal-power` solo para gain/pan, evaluado por una tabla fija del reproductor. `EnvelopePoint` mide 12 bytes: `time_from_event:u32`, `value_q16_16:s32`, `reserved:u32` (cero en v3.0). Gain usa Q8.8 dentro de Q16.16; pitch usa semitonos Q8.8 sign-extended; pan usa Q1.15. Los tiempos son estrictamente crecientes y menores que la duración del evento. La curva equal-power y su precisión forman parte del reproductor, no del archivo, y quedan fijadas por la major version.

La ganancia efectiva se evalúa en este orden y con producto ancho, redondeo al final y saturación:

```text
gain = unit.reference_gain × track.gain × event.gain
       × track_gain_envelope(t) × event_gain_envelope(t) × song_master
sample_out = saturate(sample_unit × gain)
```

El Mixer Photon mezcla cuatro voces software en una salida, pero su `MixerEffect` actual no expone pitch ni ganancia dinámica por voz. El backend debe remuestrear y hornear esos controles por bloques PCM temporales, o extender el Mixer con controles explícitos antes de afirmar automatización de alta resolución en esa ruta. El archivo conserva la automatización con independencia del backend.

### Cues y sucesos puntuales

Layout `Cue` de 32 bytes: `time_sample:u64`, `code:u16`, `value:s32`, `flags:u16`, `track_id:u16`, `event_index:u32`, `payload_offset:u32`, `payload_length:u32`, `reserved:u16`. `track_id=0xffff` indica cue global; `event_index=0xffffffff` indica que no pertenece a un evento. Payload vacío requiere offset y longitud cero; payload no vacío referencia un rango dentro de `CuePayloadBytes`. Cues en la misma muestra se despachan en orden de tabla. El juego recibe mensajes desde el drenaje cooperativo, nunca desde la IRQ. El fin del evento es exclusivo; en la misma muestra se procesan finales, inicios y luego cues.

### Síntesis y capas residuales

`SynthesisParams` mide 28 bytes: `unit_id:u32`, `fundamental_hz_q16_16:u32`, `phase_q0_32:u32`, `first_partial:u32`, `partial_count:u16`, `waveform:u8`, `flags:u8`, `level_q8_8:u16`, `wave_table_id:u16`, `reserved:u32`. `waveform`: `0=seno`, `1=tabla custom` (requiere `WaveTables`), `2=triangular`, `3=cuadrada`; el resto se rechaza en v3.0. Las tablas estándar son de 256 muestras; una custom usa entre 16 y 1024 muestras y longitud potencia de dos. Cada `Partial` mide 8 bytes: `ratio_q8_8:u16`, `amplitude_q1_15:s16`, `phase_q0_32:u32`. `representation=Additive` genera PCM desde el oscilador; `Hybrid` suma ese PCM con segmentos residuales `role=1..3`; `PCM` usa `role=0`. El residuo se satura solo al combinar componentes. `phase` es fase inicial; la continuidad entre eventos no se infiere.

### Catálogo de variantes y validación

Las dimensiones combinables son independientes, salvo las restricciones declaradas arriba:

| Dimensión | Variantes v3.0 | Restricción |
|---|---|---|
| Representación de unidad | PCM / aditiva / híbrida | Aditiva requiere `SynthesisParams`; híbrida requiere al menos un segmento residual |
| Códec por segmento | PCM8, DeltaRLE, ZX0, DeltaZX0, aPLib, Fibonacci, IMA, QuantizedPCM | Cada segmento puede usar uno distinto; IMA/Fibonacci son lossy y se miden tras decode |
| Eventos | One-shot / secuencia / loop de rango | Máximo de voces y memoria se decide antes de iniciar |
| Control | Ganancia fija / envolvente gain / envolvente pitch / ambas | Punto relativo al evento; los destinos hardware pueden reducir resolución temporal |
| Señalización | Sin cues / cues puntuales con payload pequeño | Se despacha fuera de IRQ; cues repetidos siguen la política del loop |
| Ruta | Auto / Paula preferida o requerida / Mixer preferido o requerido | Planner puede rechazar Required si falta capacidad |

El parser comprueba todas las secciones y extents antes de iniciar la reproducción; valida códec, parámetros, índices, segmentos sin huecos/superposición, offsets al payload, unidades referenciadas, loops, envolventes y cues. Ningún valor del archivo se convierte directamente en índice de tabla sin comprobarlo.

### Contrato de reproducción en Amiga

La configuración objetivo de música ACP1 ocupa los cuatro canales físicos de Paula así: `AUD0` es la salida del Mixer Photon, con hasta cuatro voces software; `AUD1..AUD3` son hasta tres voces DMA directas. El Mixer es un bus hardware, no cuatro canales Paula adicionales. La configuración estándar del repo (`MIXER_SINGLE=1`, `mixer_output_channels=AUD0`, cuatro voces) confirma este reparto.

```text
tracks ACP1 ── planificador ──┬─ hasta 3 tracks Paula directos ── AUD1/AUD2/AUD3
                              └─ hasta 4 voces software ── Mixer ── AUD0
SFX ─────────────────────────── comparte las voces del Mixer según la política de prioridad
```

El reproductor trabaja por fases:

1. **Parsear y planificar antes de tocar DMA.** Valida ACP1 completo; calcula concurrencia por intervalo; asigna primero las pistas `Required`, después preferencias y `Auto`. Reserva Chip para buffers DMA y scratch de mezcla, y Fast (si existe) para segmentos/decoder. Si un requisito no cabe, devuelve el motivo sin arrancar audio.
2. **Decodificar/preparar cooperativamente.** Mantiene un reloj de timeline en muestras. El productor busca el siguiente límite entre evento, cue, control de envolvente y fin de chunk; decodifica los segmentos necesarios, remuestrea pitch mixer si procede, hornea la ganancia que el mixer no puede representar y deja un buffer listo. No se reconstruye la obra entera en Chip.
3. **Emitir en Paula directa.** Usa `AUDxLCH/LCL/LEN` para segmentos alineados a palabra, `AUDxVOL` para nivel y `AUDxPER` para pitch. Cambiar pitch modifica la frecuencia de lectura y por tanto la duración efectiva; el planner debe convertir el reloj original a longitud de salida y mantener el calendario común. AHRM cap. 5 §“Joining Tones” permite preparar el siguiente segmento después de que DMA haya copiado location/length a sus registros de respaldo (`:4374-4380`); eventos muy cortos se agrupan o se rechazan si el coste de IRQ excede el presupuesto.
4. **Emitir por Mixer.** Hasta cuatro eventos simultáneos usan los slots software del Mixer, cuyo AUD0 consume una sola salida Chip. Pitch se remuestrea fuera de la IRQ; ganancia dinámica se hornea en el bloque PCM o usa una futura extensión explícita de Mixer. SFX compite por el mismo pool de cuatro voces mediante prioridad/reserva; ningún efecto desplaza una pista `Required` en silencio.
5. **Atender IRQs con propietario único.** El mixer Photon actual instala directamente el vector nivel 4 `$70`, que también sirve a `AUD1..AUD3`; no puede convivir sin cambios con el handler `AmigaBackend::set_audio_service`. ACP1 v3 requiere un dispatcher único/IRQ callback cooperativo que atienda completions del mixer y de las voces directas, reconozca todos los bits `AUD0..AUD3` y nunca decodifique, mezcle ni publique mensajes en ISR. Hasta integrar ese dispatcher, playback ACP1 es exclusivo respecto a Photon y los reproductores tracker.
6. **Despachar cues y errores desde el bucle.** El ISR marca contadores/flags atómicos de byte/palabra; el bucle consume cues en orden, publica mensajes, detecta underruns y finaliza handles. `AudioEnd` solo se emite al completar timeline/loop; un buffer repetido por DMA no equivale a evento de gameplay.

Paula tiene cuatro canales, no siete. La prioridad de asignación no equivale a número de tracks: una obra puede contener más tracks seriales que canales si su concurrencia máxima cabe. Si coinciden más de tres voces `PaulaRequired` o más de cuatro voces Mixer requeridas, la preflight falla indicando tracks y el intervalo causante. No hay fallback de voz ni robo silencioso. La combinación con tracker clásico usa su máscara real; `AudioMode::Game` reserva hoy AUD0 al Mixer y AUD1..3 a música, así que ACP1 puede ocupar esos tres directos más el mismo bus Mixer, sujeto al dispatcher y presupuesto de voces.

El contrato hardware se apoya en AHRM 3.ª cap. 5: dirección de muestra word-aligned y en Chip RAM (`:4078-4080`), `AUDxLEN` en palabras (`:4128`), periodo dentro de límites DMA (`:4166-4197`), canales programables independientes (`:4427-4430`) y latencia de registros respaldados al inicio de bloque (`:4374-4380`).

El análisis estructural host usa HPSS por STFT radix-2, ventana Hann y máscaras complementarias a partir de medianas temporales/frecuenciales. `--hpss` separa cada canal WAV en componentes armónica y percusiva; la comparación siempre reconstruye la mezcla ACP1 y reporta MSE y pico respecto de la mezcla normalizada. El encoder divide cada stem en unidades AUZX y emite eventos secuenciales; unidades idénticas de misma longitud se comparten byte a byte. En la evaluación de `Rondo_alla_turca.ogg` (4.004.352 muestras, IMA a 22.050 Hz, bloques de 2.048), ACP1 sin HPSS ocupó 4.302.800 bytes con MSE 0,5583/pico 19, mientras el AUZX lineal ocupó 2.025.680 bytes con MSE 0,4990/pico 29. Con HPSS, ACP1 ocupó 8.555.448 bytes y la mezcla tuvo MSE 17,2824/pico 43. Para esta entrada HPSS empeora tamaño y reconstrucción y no se selecciona automáticamente. La deduplicación aproximada requiere firmas perceptuales, comprobación de fase y evaluación de calidad y sigue desactivada; el ruteo automático queda para la planificación de voces.

Las uniones aplican fade lineal o equal-power de 10 a 50 ms. En Paula se usan rampas de volumen, doble voz temporal o un buffer pequeño; en el mixer se usa el buffer de mezcla. El crossfade no se ejecuta completo dentro de la IRQ. La fase fundamental se conserva en la unidad y en el evento: una repetición concatenada puede reanudar el acumulador de fase o forzar un punto de fase compatible; si no puede garantizarse continuidad, el encoder no reutiliza la unidad o añade un crossfade explícito.

### Ganancia y envolventes (contrato objetivo v3)

ACP1 separa la señal almacenada de los controles de reproducción:

```text
sample_final[n] = saturate(
    sample_unit_normalized[n]
    × unit_gain × event_gain × track_gain(t)
    × music_gain(t) × master_gain(t))
```

Todos los factores usan una escala fija común, preferentemente `Q0.8` o el tipo fixed seleccionado por la configuración. El producto se acumula en una representación más ancha y se normaliza una sola vez antes de escribir al buffer Paula/mixer. La envolvente puede ser una rampa lineal, equal-power o una tabla de puntos; la tabla global se comparte entre eventos.

- `unit_gain`: normaliza el prototipo almacenado.
- `event_gain`: recupera el volumen de cada aparición.
- `track_gain(t)`: automatización de una pista o stem.
- `music_gain(t)`: fade-in/fade-out y volumen de la obra.
- `master_gain(t)`: volumen global, mute y fades globales.

El volumen final se satura al rango del destino. En Paula se convierte a `AUDxVOL` y en el mixer se aplica antes de sumar voces. El layout v1/v2 no contiene estos niveles de automatización: solo su campo de ganancia de evento constante.

## Pseudocódigo del análisis estructural

El análisis completo se ejecuta en PC. La duración del fichero no obliga a retener toda la señal: la ingestión entrega ventanas y el entrenador reutiliza scratch de hasta el presupuesto configurado.

```text
para cada capa de entrada:
    leer ventana PCM normalizada
    calcular STFT con ventana Hann
    separar máscara armónica/percusiva mediante medianas espectrales
    reconstruir harmónico, percusivo y residual
    dividir opcionalmente cada capa en sub, low-mid, mid y high
    detectar onsets/beats o usar ventanas de 0,5..4 s
    normalizar ganancia y conservar unit_gain
    estimar fase fundamental y continuidad de borde
    firmar cada unidad con energía, espectro, chroma, envolvente, fase y duración
    buscar unidades idénticas o suficientemente similares
    conservar una copia del prototipo y emitir referencias para las repeticiones
    clasificar cada unidad como Paula o mixer
    probar candidatos de codec y conservar tamaño + error reconstruido
generar ACP1 con tablas, unidades, tracks y eventos
```

La máscara HPSS usa dos medianas: una horizontal sobre el tiempo favorece componentes armónicas sostenidas y otra vertical sobre frecuencia favorece ataques percusivos. La separación se reconstruye aplicando las máscaras a la magnitud STFT y reutilizando la fase original.

```cpp
// Esqueleto C++23 host; las matrices reales se procesan por ventanas.
auto stft = analyze_stft(window, config.n_fft, config.hop);
auto magnitude = abs(stft);
auto harmonic_med = median_filter_time(magnitude, config.kernel_size);
auto percussive_med = median_filter_frequency(magnitude, config.kernel_size);
for (Bin b : bins(stft)) {
    const auto h = harmonic_med[b] * config.margin;
    const auto p = percussive_med[b] * config.margin;
    const auto denominator = h + p + config.epsilon;
    harmonic_mask[b] = h / denominator;
    percussive_mask[b] = p / denominator;
}
harmonic = istft(apply_mask(stft, harmonic_mask));
percussive = istft(apply_mask(stft, percussive_mask));
residual = window - harmonic - percussive;
```

El algoritmo host puede usar bibliotecas FFT, audio y concurrencia del sistema. La interfaz que cruza al engine debe convertir el resultado a PCM8/AUZX o ACP1; el player Amiga no depende de FFT ni de las librerías host.

## Mediana deslizante

La mediana 2D ingenua ordena una ventana para cada posición. Para el entrenador se debe usar una política host reutilizable: dos particiones ordenadas o dos heaps con borrado diferido. En el core freestanding no se introduce `std::multiset`; si una demo Amiga necesita la operación, se implementará como contenedor de capacidad fija sobre `Span` y `eng::quick_sort`/búsqueda binaria.

```text
SlidingMedian<K>:
    low  = mitad inferior, máximo en la cima
    high = mitad superior, mínimo en la cima

add(value):
    insertar en low si value <= max(low), si no en high
    rebalancear hasta |size(low)-size(high)| <= 1

remove(value):
    marcar/eliminar una ocurrencia en la partición que la contiene
    limpiar eliminaciones diferidas en las cimas
    rebalancear

median():
    si tamaños iguales: (max(low)+min(high))/2
    si no: max(low)
```

El encoder debe preferir una mediana deslizante de histograma cuando cuantice la magnitud espectral a un rango fijo; así el coste puede ser O(1) por posición. La versión de dos heaps queda como referencia y para rangos no cuantizados.

## Encoder ACP1 en C++23

El encoder host puede paralelizar unidades independientes. La barrera no puede ser un `sleep`: el número de tareas pendientes se incrementa antes de encolar, cada trabajo lo decrementa al terminar y el hilo coordinador espera hasta cero. El mapa de correspondencia `(layer, block) -> unit_id` se construye antes de generar los eventos.

```cpp
struct BlockKey {
    usize layer;
    usize block;
    friend constexpr bool operator<(BlockKey a, BlockKey b) noexcept {
        return a.layer < b.layer || (a.layer == b.layer && a.block < b.block);
    }
};

for (LayerIndex layer : layers) {
    for (BlockIndex block : blocks(layer)) {
        pending_tasks.fetch_add(1, std::memory_order_relaxed);
        pool.enqueue([&, layer, block] {
            Unit unit = encode_unit(layer, block, tuning);
            {
                std::lock_guard lock(units_mutex);
                units.push_back(std::move(unit));
            }
            {
                std::lock_guard lock(map_mutex);
                unit_ids.insert({layer, block}, unit.id);
            }
            if (pending_tasks.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                barrier.notify_one();
            }
        });
    }
}
barrier.wait(lock, [&] { return pending_tasks.load() == 0; });

for (LayerIndex layer : layers) {
    Track track = make_track(destination_for(layer));
    for (BlockIndex block : blocks(layer)) {
        track.events.push({unit_ids.at({layer, block}), start(block), fade_in, fade_out});
    }
}
write_acp1(output, tables, units, tracks);
```

La implementación host puede usar `std::thread`, `std::mutex` y contenedores dinámicos porque el encoder no se ejecuta en Amiga. El formato y los valores que escribe deben pasar por serializadores little-endian explícitos; el player nunca depende del layout ABI del compilador host.

## Contrato del player Amiga

El player C++23 freestanding mantiene tablas de capacidad fija para unidades, eventos activos y voces. La IRQ no analiza eventos ni descomprime: el servicio cooperativo prepara buffers y controles; la IRQ solo reconoce completions, actualiza flags/contadores atómicos y swapea DMA.

```text
ACP1_Init(blob):
    validar cabecera, directorio, tablas, payloads y CRC opcional
    calcular solapamientos por intervalo y voices_required
    asignar Required; luego Prefer; luego Auto
    reservar Chip PCM DMA, Fast para payload/scratch si existe
    rechazar antes de tocar DMA si no hay una asignación válida

AudioService():
    determinar ventana PCM y eventos que la intersectan
    decodificar cada segmento requerido fuera de la IRQ
    sintetizar unidad aditiva si la representación lo requiere
    remuestrear pitch si se debe conservar la timeline
    aplicar gain unit × event × track-envelope × master
    renderizar/enqueue hasta 3 voces Paula y hasta 4 voces software Mixer
    actualizar cursors y acumular cues pendientes

AudioIRQ():
    despachador único reconoce bits AUD0..AUD3
    por cada buffer completado: marcar libre y contador/flag
    cambiar solo el puntero/longitud del buffer Paula directo o Mixer
    no decodificar, mezclar, recorrer cues ni llamar al juego
```

El estado runtime de cada pista incluye evento actual, cursor de muestra, región de loop, ruta asignada, pitch/gain actuales, índices de envolvente y estado. La preflight reserva hasta tres voces directas `AUD1..AUD3` y usa `AUD0` como salida única del Mixer Photon, que ofrece hasta cuatro voces software; esos cuatro slots no son canales Paula independientes. Los eventos adicionales se permiten si no se solapan en el mismo track y el pico de concurrencia cabe en esas capacidades. Se respetan las cuotas de `AudioMode`; no se pisa música tracker ni una voz requerida.

`MixerEffect` de Photon no expone pitch ni volumen por voz. ACP1 v3 define que el backend remuestree/hornée pitch y envolvente en buffers PCM temporales antes de encolar a la voz, o devuelva `UnsupportedVoiceControl` si no puede hacerlo dentro de scratch/CPU. No se cambia `AUDxPER` para ajustar una voz software; el periodo pertenece al canal de salida Mixer.

El mixer estándar instala directamente su handler en el vector 68000 de nivel 4 `$70`; las voces Paula también generan ese nivel. El backend actual no permite instalar ambos handlers concurrentemente. ACP1 requiere sustituirlos por un dispatcher único con callback del Mixer Photon y feeder de Paula directa; hasta que exista y se valide, el playback estructural es exclusivo y `audio_init()`/`composition_playback_begin()` deben rechazarse mutuamente. El límite concreto y el timing de `AUDxLEN` se describen en el AHRM cap. 5 §“Joining Tones” y `docs/reference/emulators/winuae/audio-irq.md`.

Todos los offsets del archivo se validan en 32 bits antes de hacer vistas. Las tablas runtime se copian a capacidad fija de Fast RAM o a scratch; Paula solo recibe buffers PCM en Chip RAM. La cola de cues se drena fuera de interrupción y el juego recibe los cues en orden temporal, no referencias a secciones del blob.

## Relación con las pistas

| Capa ACP1 | Destino | Política |
|---|---|---|
| Armónica, bajo, pad, lead | Paula 0..2 | Cambiar periodo para pitch; crossfade con volumen o doble voz |
| Percusión y transitorios | Mixer 0..3 o Paula fija | Evitar resampling software innecesario |
| Residual denso y ambiente | Mixer 0..3 | Buffer de mezcla y fades controlados |
| Mezcla final | Paula 3 | Buffer Chip doble/triple alimentado por `AudioFeeder` |

El número máximo de pistas no se confunde con el número de canales físicos: ACP1 describe hasta tres voces Paula directas y cuatro voces virtuales, pero el planner puede rechazar una configuración que exceda memoria, tiempo de decodificación o presupuesto de mezcla.

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

Los parámetros explorables serán frecuencia, canales de entrada, tasa de salida, tamaño de chunk, predictor, bits de cuantización, escala, dithering, noise shaping, preénfasis, codec y umbrales de tonalidad. El entrenamiento host trabaja por ventanas reutilizadas y tiene un presupuesto declarado de 6 GiB; la duración del audio no determina el consumo de RAM. Cada ensayo debe guardar la configuración completa y el hash de la entrada bajo `out/playground/audio-compressor/`. El evaluador solo puede publicar MSE/SNR si el adaptador ha reconstruido la ventana; el informe debe distinguir el tamaño comprimido de la fuente del tamaño PCM normalizado, porque comparar AUZX contra MP3/OGG mezcla dos compresiones distintas.

Los parámetros normalizados viven en `eng/audio/audio_tuning.hpp` como `AudioTuning<S>`. La misma plantilla se instancia con `float` durante la exploración o con `Fixed<s32,16>` cuando se necesita reproducibilidad y una representación apta para el runtime. Las cabeceras genéricas no arrastran `fixed.hpp`: el consumidor elige el escalar y aporta sus extensiones matemáticas. `eng/core/util/quantizer.hpp` aporta `lloyd_max` para entrenar tablas de reconstrucción con el scratch del llamador, sin heap.

La reproducción se solicita con una `PlayIntent` (`eng/audio/playback.hpp`) y devuelve un `PlaybackHandle`. El juego puede llamar a `pause(handle)`, `resume(handle)`, `stop(handle)` y `set_volume(handle, volume)` sin conocer si la sesión es un sample, un stream AUZX o una composición ACP1. El backend resuelve caché de unidades, secuencias, decodificación, buffers, mixer y canales Paula. `MediaStreamBackend` valida el medio reconocido por `media::Info`; `PcmStream` y `AudioFeeder` siguen siendo implementación interna.

Cuando el secuenciador alcanza un `AudioCue`, publica `MsgType::AudioCue` en el puerto del mini-SO desde el drenaje del bucle. El mensaje usa `payload.user.a` para el handle, `payload.user.b` para el código y `payload.user.code` para el valor; la IRQ solo registra que el punto fue alcanzado y nunca ejecuta lógica del juego.

## Restricciones del decoder Amiga

- La IRQ de audio solo cambia buffers y marca trabajo pendiente; la descompresión se realiza en el bucle o en una tarea cooperativa.
- El payload puede residir en Fast RAM, pero el PCM que lee Paula debe terminar en Chip RAM.
- El decoder C++23 será la referencia portable; los bucles medidos se podrán portar a ASM 68000 después de disponer de un test de equivalencia byte a byte y una auditoría `asm-audit.mjs`.
- La síntesis armónica usará tabla de onda y acumulador de fase; el objetivo A500 inicial es fundamental más un armónico, con un máximo de dos armónicos en la primera implementación.

## Fuentes externas y licencias

ZX0 se integra mediante el decoder del engine, atribuido a Einar Saukas. Los depackers ASM ZX0 y aPLib incorporados desde Emmanuel Marty conservan licencia zlib. IMA ADPCM sigue el estándar IMA/DVI. `ffmpeg` y `yt-dlp` son dependencias opcionales del entorno del usuario, no binarios versionados.
