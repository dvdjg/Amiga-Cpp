# HOST-239: `PcmStream` (A5) con E/S simulada

Verifica `eng::audio::PcmStream<2>` (`engine/include/eng/audio/pcm_stream.hpp`): la unión de la
máquina de estados de buffers (`eng::os::ChunkStream`) con el **codec real** (`pcm_codec`,
Delta+RLE) para el *pseudo-streaming* desde disquete
(`docs/engine/architecture/AUDIO_STREAMING.md` §3–5).

## Qué comprueba

1. **Flujo normal (doble buffer)**: `begin` deja los dos buffers libres; el llamador (aquí, el
   test, simulando la tarea de fondo) carga los dos primeros chunks con `provide`; la IRQ
   (`advance`) alterna el buffer de reproducción; el PCM que suena **byte a byte** coincide con el
   chunk decodificado; al final llega `eof` y `finished`.
2. **Underrun real**: la IRQ pide el siguiente buffer cuando nunca llegó → `advance` falso +
   `underrun`, sin confundirlo con el fin del stream (`at_end` falso).
3. **EOF**: tras el último chunk no se piden más (`free_mask == 0`); el `advance` que agota los
   buffers da `at_end`/`finished` (fin normal, no underrun real).
4. **Chunk inválido**: un flujo que no produce `chunk_samples` muestras se rechaza con `false`.

La "E/S" es simulada (el test lee de su tabla), como en HOST-257; la lectura real (`trackdisk`), la
reprogramación de Paula en la IRQ y la demo en hardware quedan como el resto de A5.

## Salida de referencia

```
OK: PcmStream (doble buffer, underrun, EOF) validado.
```

## Ejecutar

```bash
bash tools/run-host-tests.sh tests/host/audio/239_audio_stream
```
