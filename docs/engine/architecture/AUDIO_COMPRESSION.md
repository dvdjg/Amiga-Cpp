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

`ACP1` se usa cuando el audio completo tiene redundancia temporal o espectral que no conviene representar como una única onda lineal. El encoder analiza el material offline, extrae capas y unidades, y escribe una secuencia de referencias. El Amiga no vuelve a analizar el audio: resuelve eventos, decodifica la unidad solicitada y aplica el destino y la transición indicados.

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

El layout binario definitivo debe escribirse con `ByteReader`/`ByteWriter` little-endian y offsets validados; no se usará `#pragma pack` como API de parseo ni `reinterpret_cast` en el decoder. El parser comprobará magic, versión, offsets, límites de unidades, destino de pista y referencias de `unit_id` antes de reproducir.

El header ACP1 v1 contiene flags, frecuencia, número de unidades, tres pistas Paula, cuatro voces mixer, canal Paula reservado y offsets a tablas, unidades, tracks y final. Cada unidad contiene id, offset/tamaño, longitud reconstruida, modo, flags tonal/percusivo, parámetros armónicos, alpha, ganancia de referencia y estado de fase. Cada evento contiene unidad, inicio, duración, ganancia de evento, pitch fino, fade-in/fade-out y referencia opcional a una envolvente. Una tabla opcional de `AudioCue` contiene posición en muestras, código, valor y flags para eventos musicales.

El análisis estructural usa HPSS por STFT, división opcional en sub/low-mid/mid/high y firmas espectrales, chroma, MFCC o forma de onda normalizada para detectar unidades exactas o similares. La firma incluye también envolvente de amplitud y fase fundamental; dos unidades solo se deduplican si la forma normalizada, la continuidad de fase y el contrato de pitch son compatibles. La unidad se almacena normalizada a una ganancia de referencia; cada aparición conserva su ganancia original como parámetro de evento. Los armónicos, bajos y pads se proponen para Paula; percusión, ruido, residuales densos y ambientes para el mixer. La decisión se almacena como metadato y no permite reasignación silenciosa en el runtime.

Las uniones aplican fade lineal o equal-power de 10 a 50 ms. En Paula se usan rampas de volumen, doble voz temporal o un buffer pequeño; en el mixer se usa el buffer de mezcla. El crossfade no se ejecuta completo dentro de la IRQ. La fase fundamental se conserva en la unidad y en el evento: una repetición concatenada puede reanudar el acumulador de fase o forzar un punto de fase compatible; si no puede garantizarse continuidad, el encoder no reutiliza la unidad o añade un crossfade explícito.

### Ganancia y envolventes

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

El volumen final se satura al rango del destino. En Paula se convierte a `AUDxVOL` y en el mixer se aplica antes de sumar voces.

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

El player C++23 freestanding mantiene tablas de capacidad fija para unidades, estado de pistas y voces. La IRQ no analiza eventos ni descomprime: el servicio cooperativo prepara el siguiente buffer y la IRQ solo cambia el buffer listo.

```text
ACP1_Init(blob):
    parsear cabecera y validar offsets
    indexar UnitHeader en tabla fija
    validar TrackHeader y cada referencia unit_id
    reservar/recibir buffers Chip para Paula y buffer mixer

AudioService():
    para cada track cuyo start_sample <= reloj:
        resolver UnitHeader
        decodificar unidad fuera de la IRQ
        combinar unit_gain × event_gain × track_gain × music_gain × master_gain
        aplicar envolvente/fade y preparar destino Paula/mixer
    mezclar hasta cuatro voces software en el buffer Chip del canal reservado
    publicar IntentDone o AudioUnderrun en el puerto

AudioIRQ():
    avanzar AudioFeeder
    cambiar puntero/longitud del buffer Paula
    marcar buffer libre
```

El estado de cada pista debe contener como mínimo evento actual, posición de muestra, muestras restantes, volumen, periodo, destino y estado activo. Los offsets del archivo se expresan como `u32`; los índices y límites se validan antes de convertirlos a índices de capacidad fija. Paula solo recibe buffers Chip; los payloads comprimidos y las tablas pueden permanecer en Fast RAM.

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
