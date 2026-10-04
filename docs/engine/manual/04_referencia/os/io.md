# Referencia — E/S, VFS y streaming

E/S asíncrona del mini-SO y la fachada de sistema de archivos. El **contrato** vive aquí; la **implementación** la aporta el backend (`dos.library`/`trackdisk`) o, en tests, un backend simulado en memoria.

## E/S asíncrona — `os/file.hpp`

`FileMode` (`file.hpp:19`: `Read`/`Write`/`ReadWrite`/`Create`), `FileOp` (`:22`) y `FileHandle` (`:24`, `0` = inválido) son el vocabulario. El buffer va como **vista** (`Span`) y el cookie discrimina al consumidor.

`IoUser` (`:30`) es el **cookie de E/S**: `tag` ('A' asset, 'L' lib, 'S' stream) + `id` + **generación**; `encode()`/`decode()` lo empaquetan en `u32` como `tag(8) | generation(8) | id(16)`. `IoNotify` (`:49`) dice cómo notificar (mensaje al puerto, prioridad, cookie). `FileDonePayload` (`:56`) es el payload del mensaje `FileDone`.

## VFS — `os/vfs.hpp` y `os/path.hpp`

`path.hpp` normaliza paths (función **pura**, sin E/S): colapsa separadores, resuelve `.`/`..` y **rechaza** escapar por encima de la raíz. `normalize_path(src, out)` (`path.hpp:35`) devuelve la longitud escrita o un `PathError` (`Empty`/`TooLong`/`EscapesRoot`/`TooManyComponents`); `kMaxPathComponents = 64` (`:32`). El **dispositivo** (`DF0:`, `PROG:`) no se interpreta aquí.

`Vfs<Backend>` (`vfs.hpp:88`) es la fachada de alto nivel: `exists`/`size`/`read`/`read_all`/`list`/`open` sobre un **backend** (concepto de plantilla, no `void*`) que aporta `exists`/`size`/`read` (y opcionalmente `list`/handles, detectados con `requires`). Aporta **montajes** (prefijo lógico → raíz del backend) y una **raíz** por defecto, para que el juego use paths **lógicos** sin codificar el volumen. `VfsError` (`:32`) es el error normalizado; `DirEntry` (`:48`) la entrada de `list`; `kVfsPathMax` (`:45`) el tamaño de buffer.

## Generaciones — `os/request.hpp`

`RequestTable<MaxSlots>` (`request.hpp:29`) separa la identidad de una petición (`RequestId` = slot + generación + `kind`) del `IoUser`, de modo que la **respuesta tardía** de una operación antigua **no** complete el slot de una nueva que lo reutilizó. `acquire(kind)` (`:40`) abre (reutiliza slot con la generación siguiente), `alive(r)` (`:48`) valida, y `complete`/`cancel` cierran. Es **puro** (host-testable).

## Streaming — `os/stream.hpp` y `os/file_stream.hpp`

`ChunkStream<NumBuffers>` (`stream.hpp:18`, 2..8) es la máquina de estados de un flujo con **doble/triple buffer**: Paula consume el buffer de reproducción mientras el disco llena el siguiente. `on_chunk_ready(idx)` (`:30`) marca un buffer lleno, `play_ready()`/`play_index()` (`:35`, `:38`) consultan, `advance()` (`:44`, llamado por el IRQ de audio) pasa al siguiente y detecta **underrun**, `request_mask()` (`:53`) da los buffers **vacíos** a leer. `FileChunkFeeder<NumBuffers, Source>` (`file_stream.hpp:26`) alimenta un `ChunkStream` desde un fichero asíncrono.

## Disco de bajo nivel

`trackdisk.hpp` accede a `trackdisk.device` a nivel de device (`TdHandle`/`TdGeometry`). `floppy.hpp` es control mecánico por CIA-B y DMA crudo de Paula: `mfm_decode_long`/`mfm_decode_byte`/`mfm_word_at`/`mfm_xor_long_at`/`sector_checksums_ok`/`floppy_find_sector` y las constantes `kMfmSync = 0x4489`, `kSectorBytes = 512`, `kMfmWordsPerSector = 544` (`floppy.hpp:34`).

Volver al [índice de `os/`](README.md) · [Referencia](../README.md) · [manual](../../README.md).
