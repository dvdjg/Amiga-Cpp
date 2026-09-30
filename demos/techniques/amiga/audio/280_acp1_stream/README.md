# Demo 280: reproducción exclusiva de una composición ACP1

Reproduce una composición ACP1 v2 desde el volumen `DH1:`. La tarea de frame mezcla eventos a tres buffers PCM en Chip RAM; la IRQ de nivel 4 solo avanza el feeder y cambia el buffer de Paula. Esta demostración usa el canal 3 y no inicializa el mixer Photon ni música de tracker porque comparten el vector de audio del 68000. La demo está NO VERIFICADA en emulador mientras falle el build wrapper de Windows antes de compilar las unidades.

## Preparar el volumen

Desde la raíz del repositorio, crear un WAV PCM8 estéreo corto (dos stems) y convertirlo a ACP1:

```bash
host-tools/audio-compressor/audio-compressor tema.wav --mode music --force --out out/assets/audio-compressor/tema.acp1
node tools/fs/make-volume.mjs --no-adf --add out/assets/audio-compressor/tema.acp1:data/audio/theme.acp1
```

La demo lee `data/audio/theme.acp1` con DOS antes de tomar el control del display; no necesita imagen ADF. Con la prueba `Rondo_alla_turca.ogg`, la salida ACP1 v2 conserva secuencias, pero todavía es mayor que el AUZX lineal; consulta el informe de corpus del roadmap antes de elegir esta ruta por compresión.

## Build y ejecución

```bash
bash tools/build/build-demo.sh demos/techniques/amiga/audio/280_acp1_stream --release --clean
bash tools/run/run-demo.sh demos/techniques/amiga/audio/280_acp1_stream --warp
```

El criterio de éxito es llegar a `READY` con chunks cargados, swaps de buffer y cero underruns. La decodificación y mezcla ocurren fuera de la IRQ. El backend rechaza iniciar el servicio si el mixer o la música tracker ya posee el nivel 4; la planificación automática de voces permanece pendiente. La prueba de compilación cruzada directa de la unidad de demo pasa; `build-demo.sh` no ha llegado a compilarla porque GCC falla al crear el archivo `.d` con este path de Windows, así que la validación en WinUAE sigue pendiente.
