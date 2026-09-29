# `audio-compressor`

Aplicación única de PC para transformar y comprimir audio para Amiga. El ejecutable acepta un WAV/RAW arrastrado sobre él o una CLI explícita. No se encadenan herramientas de usuario: la ingestión, clasificación, codecs, métricas e informe pertenecen a esta aplicación.

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

El ejecutable resultante enlaza SDL3 de forma estática. La comprobación de Windows debe mostrar solo APIs del sistema (`KERNEL32`, `USER32`, `WINMM`, etc.) y nunca `SDL3.dll`, `libstdc++-6.dll` o `libgcc_s_seh-1.dll`. El runtime `libwinpthread-1.dll` puede aparecer si el toolchain no proporciona su archivo estático; no pertenece a SDL3 y debe resolverse instalando el paquete de runtime estático de MinGW o compilando con un toolchain que lo incluya.

## Uso rápido

```bash
audio-compressor tema.wav
audio-compressor disparo.wav --mode sample --codec ima --out out/assets/audio/disparo.auzx
audio-compressor tema.wav --mode music --dry-run --report out/playground/audio-compressor/tema.json
audio-compressor disparo.wav --play
```

Sin opciones, la aplicación genera una salida junto al archivo de entrada, no sobrescribe archivos existentes y aplica defaults seguros. El modo `music` informa que ACP1 aún requiere el módulo estructural C12; no genera un contenedor incompleto.

La configuración usa JSON plano con claves `mode`, `codec`, `sample_rate`, `chunk_samples`, `ram_budget_bytes`, `window_samples` y `force`. La precedencia es `defaults < config < CLI`.

## Reproducción host con SDL3

La reproducción es opcional. Sin SDL3, la utilidad sigue funcionando para conversión y devuelve un error claro si se usa `--play`. El build preferido usa enlace estático:

```bash
SDL3_ROOT=/ruta/al/SDL3 bash host-tools/audio-compressor/build.sh --sdl3
```

`--play` reproduce la señal normalizada de entrada. Si la entrada ya es AUZX, la aplicación la decodifica y reproduce su PCM reconstruido. ACP1 se añadirá cuando esté implementado el player estructural.
