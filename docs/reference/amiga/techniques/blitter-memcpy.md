# Copia lineal por Blitter (`memcpy`)

`MinimalBackend::blitter_memcpy(dst, src, wait)` copia RAM **arbitraria** (no planar) con el
Blitter: `D = A` (minterm `$F0`), módulos 0, palabras contiguas. Para copias **planar**
(bitplanes con stride/módulos) usar el seam `Rasterizer::copy_rect` / `FramePlan` (ver
[`sprite-layer.md`](sprite-layer.md) y `field/raster.hpp`).

## Cuándo conviene frente a una copia CPU

| Situación | Mejor opción |
|---|---|
| Hay trabajo de CPU que **solapar** con la copia | **Blitter** (`wait=false` + trabajo CPU) |
| Copia grande y contigua, sin solape | Blitter (un blit libera el bucle) **o** CPU; el bus es el mismo |
| Copia pequeña | **CPU** (`copy_rect_cpu`/`core/util/binary.hpp`): el overhead de programar el Blitter no compensa |
| El cuello es el **ancho de banda** | cualquiera: el Blitter **no** da más bus que la CPU |

Clave: en un A500 Blitter y CPU **comparten el bus**. El Blitter **no** copia "más rápido" per
se; gana cuando **libera la CPU** para hacer otra cosa en paralelo (o cuando evita un bucle de
CPU con *stalls* de bus).

## Modo asíncrono

`wait = false` lanza la copia y vuelve. Para saber si terminó:

- **Polling**: `backend.blitter_busy()` (bit `BBUSY` de `DMACONR`).
- **Servicio de fondo**: `backend.set_blitter_service(fn, user)` — `wait_blitter()` lo **drena**
  mientras espera (patrón de la demo `081_background_tasks`). El servicio puede encolar el
  aviso en la cola del SO (`task::BackgroundQueue`, o el mini-SO de mensajes `eng::os`) o
  marcar una bandera que el juego consulte.
- **Sincronización obligatoria**: `wait_blitter()` (o `blitter_busy() == false`) **antes** de
  leer el destino o de relanzar el Blitter.

## Límites

- Solo **palabras completas** (se descarta un byte impar final).
- Fuente y destino accesibles por **DMA** (Chip/Fast). ROM no: habría que pasar por CPU.
- **No** solapar origen y destino (el Blitter no lo garantiza).
- Chipset: `BLTSIZE` = anchura (6 bits, 1..64 words) × altura (10 bits, 1..1024); la rutina
  encadena varios blits para copias mayores.
