# HOST-306: feeder fichero → `ChunkStream` (M8)

Test host de `eng/os/file_stream.hpp` (`FileChunkFeeder`): alimenta un `ChunkStream` desde un
fichero **asíncrono** lanzando lecturas **secuenciales** por cada buffer vacío.

## Qué comprueba

1. **Arranque**: `pump` lanza una lectura por buffer vacío (offsets 0 y `chunk`); no re-lanza una
   lectura ya en curso.
2. **`on_done`**: marca el buffer listo; el contenido coincide con el fichero.
3. **Avance**: al consumir el buffer de reproducción, `pump` lanza el siguiente chunk.
4. **EOF**: lectura corta / offset agotado → `eof`; al vaciarse todo con `eof` → `finished`.
5. **Underrun** (`ChunkStream::advance` sin datos) e índices inválidos.

## Notas

- El `ReadFn` (lectura asíncrona) se **inyecta**: el backend aporta `file_read_async`; el test, un
  fake síncrono. Así la política de buffers es host-testable sin emulador ni fichero.
- El fichero de prueba es de 28 B (3 chunks + 4 B) para cubrir la lectura corta/EOF.
- **Pendiente**: la demo en hardware que lee el fichero grande (512 KB) de la imagen de disquete
  por rebanadas y lo reproduce (`PcmStream`).

## Salida de referencia

```
OK: feeder fichero->ChunkStream (secuencial, EOF, underrun) validado.
```

## Ejecutar

```bash
bash tools/run-host-tests.sh tests/host/306_os_file_stream
```
