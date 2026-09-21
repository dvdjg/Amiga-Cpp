# Mini-SO: E/S asíncrona y streaming (`eng::os` io)

Cómo el mini-SO **unifica la terminación de E/S** en la misma cola de mensajes que el VBlank y la
entrada ([`MINI_OS_MESSAGE_LOOP.md`](MINI_OS_MESSAGE_LOOP.md)), de modo que la app lanza una lectura
y sigue; el resultado llega por `MsgType::FileDone`/`FileError`. El mini-SO **no implementa un
filesystem**: adapta la E/S del sistema (o del disco) a mensajes.

El detalle del *loader* (carga de chunks, trackloader de hardware, MFM) está en
[`STREAMING_LOADER.md`](STREAMING_LOADER.md); la parte de audio, en
[`GAME_AUDIO.md`](GAME_AUDIO.md). Este documento cubre la **capa de E/S asíncrona y el streaming**.

## 1. ¿Kickstart o a mano?

| Enfoque | Cuándo | Pros | Contras |
|---|---|---|---|
| **`dos.library` + `trackdisk.device`** | juego que toma la pantalla pero deja Exec vivo | rutas, OFS/FFS, HD/ADF, async con `IORequest` real | más RAM, dependencia del OS |
| **`trackdisk` solo** (sin DOS) | lectura por cilindro/sector de disquetes propios | async natural, footprint medio | sin archivos lógicos salvo tu tabla |
| **MFM a pelo** (CIA + custom) | demo/cracktro, formato propio | máximo control, mínimo OS | mucho trabajo, solo tu layout |

Decisión práctica: **HD y desarrollo** → capa fina sobre `dos.library`; **streaming desde disquete
en A500 con footprint mínimo** → `trackdisk` async + tabla de archivos propia; **máquina 100 %
tomada sin Exec** → loader MFM propio (ver `STREAMING_LOADER.md`). Implementar OFS/FFS + directorios
a mano **no compensa**; implementar **cola de pedidos de sector + doble buffer + mensajes** sí.

## 2. API orientada a mensajes

```cpp
// eng/os/file.hpp
namespace eng::os {

enum class FileMode : eng::u8 { Read, Write, ReadWrite, Create };
enum class FileOp   : eng::u8 { Read, Write, Seek, Create, Delete, Rename };

using FileHandle = eng::u16;  // 0 = inválido

/// Notificación de una operación: mensaje al puerto y/o callback en contexto de app.
struct IoNotify {
	bool post_message = true;   ///< → cola del mini-SO (FileDone/FileError)
	MsgPrio prio = MsgPrio::Low;///< la E/S no debe pisar la entrada
	void (*callback)(const FileDonePayload&) = nullptr; ///< opcional, NO desde la ISR
};

FileHandle file_open(const char* path, FileMode mode);
void       file_close(FileHandle h);
bool       file_delete(const char* path);              ///< sync o async según backend
bool       file_rename(const char* old_path, const char* new_path);

/// Asíncrona: vuelve al momento; el resultado llega por mensaje/callback.
bool file_read_async (FileHandle h, eng::Span<eng::u8> dst, eng::u32 offset, const IoNotify& n = {});
bool file_write_async(FileHandle h, eng::Span<const eng::u8> src, eng::u32 offset, const IoNotify& n = {});

/// Síncrona (bloquea esperando el FileDone de este handle): solo herramientas o load screens.
eng::s32 file_read_sync (FileHandle h, eng::Span<eng::u8> dst, eng::u32 offset);

eng::u32 file_size(FileHandle h);
bool     file_is_busy(FileHandle h);

} // namespace eng::os
```

El buffer va como **vista** (`eng::Span`) y no como puntero crudo, para mantener el tipado de
dominio del engine (`INTERNAL_TYPE_SYSTEM.md`). El payload del mensaje lleva `handle`, `result`
(bytes o error), `op` y un **cookie** del llamador que discrimina el consumidor: la caché de
assets, el loader de código o un stream. El cookie se empaqueta con una etiqueta para no confundir
consumidores:

```cpp
/// Cookie de E/S: quién pidió la operación y con qué id.
struct IoUser {
	eng::u8  tag;  ///< 'A' asset (caché), 'L' lib (loader), 'S' stream
	eng::u16 id;
};
```

Los mensajes de E/S (`FileDone`/`FileError`) los consume la fachada de recursos
([`RESOURCE_SYSTEM.md`](RESOURCE_SYSTEM.md)), que los enruta al subsistema que corresponda según el
`tag`.

## 3. Backend con Kickstart (Exec vivo)

AmigaOS ya es asíncrono (`DoIO`/`SendIO` + `Wait`/`GetMsg` sobre un `MsgPort` de Exec), pero
`Read()` de `dos.library` en el **mismo task del juego bloquea**. La integración limpia es no
bloquear el bucle de mensajes:

1. **Task Exec auxiliar** ("io task") de baja prioridad que hace `Open/Read/Write` y, al terminar,
   postea `FileDone` al puerto del mini-SO.
2. **`trackdisk.device`** con `SendIO` y un `ReplyPort` de Exec cuya llegada se traduce a
   `MsgType::FileDone`.

```text
  app: file_read_async(h, buf, off)
        │
        ▼
  FileService (dos.library o trackdisk)
        │  (SendIO / io task)
        ▼
  IRQ / reply de Exec ──► post FileDone{handle,result,op,cookie} ──► SigFile
        │
        ▼
  app: on_file_done() → registra decodificado en BackgroundQueue
```

## 4. Backend trackdisk (streaming en disquete)

Para streaming, el `FileService` traduce `(offset, len)` a **cilindro/sector** y usa `SendIO`; al
completar, el reply postea `FileDone`. Tu "FS" puede ser solo un **directorio en el track 0**
(`nombre, track_inicio, sector, longitud`), sin OFS/FFS: footprint mínimo y lectura predecible.

```text
  app: stream_request(offset, len, buf)
    → FileService traduce a cilindro/sector
    → SendIO(CMD_READ) a trackdisk
    → (motor, seek, DMA)
  IRQ/reply → post FileDone
  app: rellena el siguiente buffer
```

## 5. Streaming continuo (audio desde disquete)

Objetivo: **doble (o triple) buffer** con lecturas adelantadas; footprint = N buffers pequeños, no
el archivo entero. Paula consume un buffer mientras el disco llena el siguiente.

```text
        ┌──────────┐   play    ┌──────────┐
        │ buf A    │ ◄──────── │ Paula    │
        └────▲─────┘           └──────────┘
             │ FileDone
        ┌────┴─────┐
        │ buf B    │ ◄── trackdisk leyendo el siguiente chunk
        └──────────┘
```

```cpp
struct StreamConfig {
	FileHandle file;
	eng::u32   chunk_bytes;   ///< 2–4 KB (múltiplo de sector 512)
	eng::u8    num_buffers;   ///< 2 o 3
	eng::Span<eng::u8> buffer_mem; ///< num_buffers * chunk_bytes (Chip si va a audio)
	IoNotify   notify;
};

struct AudioStream {
	StreamConfig cfg {};
	eng::u8 play_idx = 0, load_idx = 0;
	eng::u32 file_pos = 0, file_size = 0;
	eng::u8 ready_mask = 0;   ///< bit i = buffer i lleno
	bool eof = false, underrun = false;
};

bool stream_start(AudioStream& s, const StreamConfig& cfg);
void stream_on_file_done(AudioStream& s, const FileDonePayload& p); ///< marca listo y pide el siguiente
void stream_on_audio_irq(AudioStream& s);                            ///< Paula agotó un buffer: swap
```

Lógica: `stream_start` lanza la lectura de todos los buffers menos uno; cada `FileDone` marca el
buffer como listo y pide el siguiente chunk; la IRQ de audio (o el fin del periodo de Paula) avanza
`play_idx` y libera un buffer para rellenar. Si el buffer de reproducción no está listo → `underrun`
(silencio o repetir el último).

## 6. Footprint y disquete

- **Chunk de 2–4 KB**: el motor lee 1–2 sectores por petición; no cargues tracks enteros si no hace
  falta.
- **Interleave / orden en disco**: audio en **tracks contiguos** y en el orden de reproducción para
  minimizar *seeks*.
- **Motor on**: mantener el motor encendido entre chunks de un stream (`TD_MOTOR`) para no pagar el
  *spin-up* cada vez.
- **Chip RAM**: los buffers que lee Paula en Chip; el resto del decode puede ir en Fast si hay.
- **Triple buffer**: más tolerancia a *seek*/reintentos, a cambio de +1 chunk de RAM.

## 7. Integración en el bucle y reutilización

```cpp
case MsgType::FileDone:
	if (is_stream_handle(m.payload.file.handle)) { stream_on_file_done(g_stream, payload(m)); }
	else { on_generic_load(m.payload.file); }
	break;
```

La E/S **no** introduce un segundo motor de trabajo diferido: al recibir `FileDone`, la app
**registra el decodificado** en `eng::task::BackgroundQueue`, que reparte el coste en los huecos de
VBlank (`BACKGROUND_TASKS.md`). El mini-SO solo unifica la **terminación** de la E/S en la cola.

Prioridad: la E/S va en `MsgPrio::Low` para no pisar la entrada (cola prioritaria del mini-SO).

## 8. Referencias

- `RESOURCE_SYSTEM.md` — caché de assets (LRU/prioridad) y loader de código sobre esta E/S.
- `STREAMING_LOADER.md` (loader de chunks, trackloader de hardware, MFM, `ChunkCache`).
- `GAME_AUDIO.md` (reproducción de samples y música; buffers de Paula).
- `BACKGROUND_TASKS.md` (decodificado diferido).
- ROM Kernel Reference Manual *Devices* (`trackdisk.device`) y *Includes & Autodocs* (`dos.library`).
