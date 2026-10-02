# `audio-compressor`

Aplicación única de PC para transformar y comprimir audio para Amiga. El ejecutable acepta una fuente WAV, RAW PCM8, MP3/OGG/FLAC o ProTracker MOD mediante FFmpeg y una CLI explícita. No se encadenan herramientas de usuario: la ingestión, clasificación, codecs, métricas e informe pertenecen a esta aplicación. P61/MED/AHX no forman parte todavía de la entrada soportada.

## Compilar

```bash
bash host-tools/audio-compressor/build.sh
```

Para habilitar SDL3 con enlace estático: `SDL3_ROOT=/ruta/al/SDL3 bash host-tools/audio-compressor/build.sh --sdl3`. El script busca `pkg-config --static sdl3` y después `SDL3_ROOT`/`SDL3_DIR`, y enlaza `libSDL3.a` o `libSDL3-static.a`.

### Instalación Windows reproducible

Desde Git Bash, fuera del repositorio:

```bash
git clone --depth 1 https://github.com/libsdl-org/SDL.git "$LOCALAPPDATA/Temp/opencode/sdl3-src"
cmake -S "$LOCALAPPDATA/Temp/opencode/sdl3-src" \
  -B "$LOCALAPPDATA/Temp/opencode/sdl3-build" \
  -G "MinGW Makefiles" \
  -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TESTS=OFF \
  -DSDL_EXAMPLES=OFF -DSDL_INSTALL_TESTS=OFF \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$LOCALAPPDATA/Temp/opencode/sdl3"
cmake --build "$LOCALAPPDATA/Temp/opencode/sdl3-build" --parallel 4
cmake --install "$LOCALAPPDATA/Temp/opencode/sdl3-build"
SDL3_ROOT="$LOCALAPPDATA/Temp/opencode/sdl3" bash host-tools/audio-compressor/build.sh --sdl3
```

El ejecutable resultante enlaza SDL3 de forma estática. La comprobación de Windows debe mostrar solo APIs del sistema (`KERNEL32`, `USER32`, `WINMM`, etc.) y nunca `SDL3.dll`, `libstdc++-6.dll` o `libgcc_s_seh-1.dll`. En este entorno UCRT64 `libwinpthread.a` está instalado, pero el PE sigue declarando `libwinpthread-1.dll`; es una limitación del runtime/toolchain y no una dependencia de SDL3. Eliminarla requiere un toolchain MinGW completamente estático o una configuración SDL3 sin subsistemas que la necesiten.

## Uso rápido

```bash
audio-compressor tema.wav
audio-compressor disparo.wav --mode sample --codec ima --out out/assets/audio/disparo.auzx
audio-compressor tema.wav --mode music --codec auto --compare --report out/playground/audio-compressor/tema.json
audio-compressor tema.wav --mode music --acp1-version 3 --codec none --out out/assets/audio-compressor/tema-v3.acp1
audio-compressor mezcla.ogg --synth-separate --out out/playground/audio-compressor/mezcla-synth.acp1
audio-compressor disparo.wav --play
```

Sin opciones, la aplicación genera una salida junto al archivo de entrada, no sobrescribe archivos existentes y aplica defaults seguros. El modo `music` genera ACP1 v2 para WAV PCM mono/multicanal de hasta siete pistas: conserva cada canal como eventos con línea temporal común, unidades AUZX compartidas por igualdad exacta y silencios entre eventos. Un MOD se renderiza a una mezcla PCM mediante FFmpeg antes de entrar en la misma ruta, por lo que no conserva patrones, instrumentos ni los cuatro canales como stems. `--hpss` separa cada canal WAV en componentes armónica/percusiva si el resultado cabe en siete pistas. HPSS permanece optativo porque puede aumentar el tamaño y el error. ACP1 v2 es un contenedor multipista por bloques y no genera ACP1 v3.

La configuración usa JSON plano con claves `mode`, `codec`, `sample_rate`, `chunk_samples`, `ram_budget_bytes`, `window_samples` y `force`. La precedencia es `defaults < config < CLI`. SAMPLE WAV y RAW se procesan por ventanas; `sample_rate` puede remuestrear la fuente manteniendo la fase entre ventanas.

## Directorios de trabajo y conversiones

- `out/tmp/audio-compressor/`: ejecutables, temporales y conversiones intermedias.
- `out/assets/audio-compressor/converted/`: conversiones finales AUZX/ACP1 generadas por el pipeline.
- `out/assets/audio-compressor/candidates/`: candidatas conservadas con `--keep-candidates`.
- `out/reports/audio-compressor/`: informes JSON de cada conversión.
- `out/playground/audio-compressor/`: corpus, barridos y comparativas de entrenamiento.

Cada conversión puede usar `--report out/reports/audio-compressor/nombre.json`. El informe registra entrada, modo, codec, tasa, chunks, muestras, duración, tamaño comprimido de la fuente, tamaño PCM normalizado, tamaño de salida, ratios PCM→salida y fuente→salida, MSE PCM8, SNR, pico de error y estado del round-trip.

ACP1 v2 serializa offsets y tablas explícitos, divide cada stem en unidades de `--chunk` muestras y emite eventos secuenciales. Bloques AUZX idénticos de la misma longitud se comparten en el diccionario. El informe compara bytes lineales/estructurales y reconstruye la mezcla para medir MSE y pico. `--codec auto` y `--compare` prueban `none`, Delta+RLE, Fibonacci e IMA ADPCM sobre la señal normalizada y muestran tamaño, MSE y pico; la selección automática elige el menor tamaño sin ocultar esas métricas. Para conservar ambas salidas, usar `--keep-candidates`; la salida `.linear.auzx` se elimina por defecto después de generar ACP1.

`--acp1-version 3` activa el MVP binario de ACP1 v3: escribe unidades PCM8, un segmento por unidad, payloads absolutos, tracks y eventos, y valida el archivo recién escrito con el parser v3. No incluye todavía síntesis, envolventes, cues, codebooks, wavetables ni reproducción Amiga v3. El MVP se usa para cerrar el contrato binario antes de conectar los codecs por segmento y el planner.

`--synth-separate` activa la vertical experimental de síntesis durante la reproducción: decodifica la fuente, estima candidatos de frecuencia fundamental y parciales por ventanas, genera ACP1 v3 aditivo y recompone las ventanas con `Acp1HostPlayer` para medir MSE, SNR, pico y correlación entre pistas. `--synth-export-dir dir` escribe `mix.wav` y `track-N.wav`; `--synth-listen N` escucha una pista aislada cuando el ejecutable se compiló con SDL3 (`bash host-tools/audio-compressor/build.sh --sdl3`). Si `--synth-max-tracks` supera siete modelos, la CLI devuelve una decisión explícita `fallback=octamed`; la integración con una playroutine MED dinámica sigue pendiente. Es un separador inicial para señales armónicas; no demuestra todavía separación instrumental fiable en mezclas densas. Para repetir un corpus: `node tools/bench-synth-separation.mjs pieza1.ogg pieza2.ogg`.

`--spectral-separate` activa la ruta experimental basada en una representación STFT tiempo-frecuencia. Cada prototipo es un prisma formado por un espectro de magnitudes, una activación por frame y un desplazamiento de bins que aproxima el movimiento de frecuencia. `--spectral-max-prototypes N` controla cuántos prototipos se extraen; `--spectral-both` ejecuta siempre las variantes de 3 y 8 para compararlas; `--spectral-target-residual R` permite detener la extracción cuando el residual relativo baja de `R`. `--spectral-export-dir dir` escribe `prototype-N.wav`, `track-N.wav`, tres ficheros AUZX por prototipo (`attack`, `sustain`, `release`) y, para hasta siete pistas, una candidata `spectral.acp1` compacta con unidades compartidas y eventos. La fase de la mezcla se sigue usando para la reconstrucción host; el renderer ACP1 PCM por eventos todavía debe completarse para reproducir esta candidata sin expandirla.

La búsqueda se puede acotar sin recompilar: `--spectral-fft N`, `--spectral-hop N`, `--spectral-max-shift-bins N`, `--spectral-seed-candidates N`, `--spectral-min-activation R` y `--spectral-max-dictionary-bytes N` controlan resolución, desplazamiento, multiarranque, esparsidad de activaciones y presupuesto del diccionario. La puntuación de cada candidato es energía explicada por coste estimado de muestra, activaciones y desplazamientos; no se conserva automáticamente el frame de mayor energía si otro candidato explica más residual por byte. Cada prototipo se prueba con `none`, `rle`, `fib` e `ima` mediante round-trip; `--spectral-codec auto` elige el menor tamaño que respeta `--spectral-max-codec-error N`, mientras que un codec explícito fuerza el mismo método para todos.

`--spectral-listen N` reproduce con SDL3 la pista temporal reconstruida del prototipo indicado; requiere `bash host-tools/audio-compressor/build.sh --sdl3`. El player host de ACP1 v3 también valida y mezcla la candidata compacta por unidades PCM compartidas, ganancia, pitch aproximado y fades de evento. Esta reproducción host es una validación de contrato y calidad, no evidencia todavía de reproducción Amiga on-target.

Para comparar ambas rutas sobre el mismo corpus: `node tools/bench-separation-comparison.mjs pieza1.ogg pieza2.ogg`. Las columnas del separador armónico son métricas PCM; las de la ruta espectral son métricas de magnitud STFT y no deben compararse como si fueran la misma función de coste.

## Reproducción host con SDL3

La reproducción es opcional. Sin SDL3, la utilidad sigue funcionando para conversión y devuelve un error claro si se usa `--play`. Cuando SDL3 está habilitado, la E/S host y el audio usan abstracciones SDL3; no se usa Win32 en la lógica de la aplicación. El build preferido usa enlace estático:

```bash
SDL3_ROOT=/ruta/al/SDL3 bash host-tools/audio-compressor/build.sh --sdl3
```

`--play` reproduce la señal normalizada de entrada. Si la entrada ya es AUZX, la aplicación la decodifica y reproduce su PCM reconstruido. La lectura y mezcla host de ACP1 queda pendiente. El pipeline windowed limita el scratch de ingestión a `window_samples`; `--ram-budget` sigue actuando como límite de seguridad para las rutas estructurales que todavía normalizan la entrada completa.
