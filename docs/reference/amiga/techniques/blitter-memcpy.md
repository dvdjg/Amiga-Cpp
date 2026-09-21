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
- **IRQ de fin de Blit (recomendado)**: el chipset genera la interrupción **BLIT** (nivel 3) al
  terminar; el backend la despacha en `level3_dispatch` (`amiga_minimal.cpp`) y llama a la tarea
  registrada con **`install_blit_service`/`set_blit_service`** (slot `ServiceSlot`). Esa tarea
  **es la notificación de fin**, opcionalmente programable: la app puede encolar un mensaje en su
  cola (`task::BackgroundQueue`) o, cuando exista, en el **puerto de mensajes del mini-SO**
  (`eng::os`; ver `engine/include/eng/os/README.md`) — o marcar una bandera. Registrar el
  servicio **solo cuando** haga falta (arma/desarma el IRQ).
- **Servicio de fondo**: `backend.set_blitter_service(fn, user)` — `wait_blitter()` lo **drena**
  mientras espera (patrón de la demo `081_background_tasks`).
- **Sincronización obligatoria**: `wait_blitter()` (o `blitter_busy() == false`) **antes** de
  leer el destino o de relanzar el Blitter.

### Ejemplo: asíncrona + puerto del mini-SO

```cpp
// Functor del servicio (la IRQ no captura lambdas): publica BlitDone en el puerto.
struct PostDone { eng::os::MsgPort<8>* port; };
void post_done(PostDone& s, eng::u16) {
    s.port->post(eng::os::Msg {eng::os::MsgType::BlitDone});
}
PostDone svc {&port};
backend.blitter_memcpy_async(dst, src, post_done, svc);   // arranca + arma la IRQ BLIT
// ... y en el bucle reactivo:
eng::os::Msg m;
if (port.pop(m) && m.type == eng::os::MsgType::BlitDone) { /* copia terminada */ }
```

`clear_blit_service()` desarma el IRQ cuando ya no se necesita.

## Límites

- Solo **palabras completas** (se descarta un byte impar final).
- Fuente y destino accesibles por **DMA** (Chip/Fast). ROM no: habría que pasar por CPU.
- **No** solapar origen y destino (el Blitter no lo garantiza).
- Chipset: `BLTSIZE` = anchura (6 bits, 1..64 words) × altura (10 bits, 1..1024); la rutina
  encadena varios blits para copias mayores.

## Concurrencia: el Blitter es **uno solo**

El Blitter es un **único recurso**: solo hay **una** operación en curso. Escribir `BLTSIZE`
(por CPU **o por Copper**) mientras hay un blit activo **aborta/clobber** el anterior.

- `blitter_memcpy` (síncrona) hace `wait_blitter()` **antes de cada blit**, así que espera a que
  el Blitter esté libre. Eso protege contra un blit **ya** lanzado, pero **no** contra uno que
  se lance **después** (p. ej. un `BLTSIZE` disparado por el Copper a mitad de frame mientras
  corre una copia asíncrona `wait=false`).
- **El engine SÍ puede disparar blits desde el Copper** (Técnica A):
  `CopperIntentKind::BlitterJob` + `Scheduler::emit_blitter_job` programan el Blitter y escriben
  `BLTSIZE` en una línea; la **ventana segura** (`set_blitter_window`) los limita a una zona sin
  blits de CPU (p. ej. el borde inferior). Requiere `COPCON`/`CDANG`
  (`docs/reference/emulators/winuae/copper.md`). Aun así hay que **serializar**:
  - no solapar la ventana del blit de Copper con los blits de CPU (`execute_frame_plan`), y
  - no usar `wait=false` si el Copper puede lanzar un blit dentro del mismo frame.
- Además de la copia lineal (`blitter_memcpy`) y del plan (`execute_frame_plan`), el backend
  expone **`blitter_submit(job, wait)`**: ejecuta **un** `graphics::BlitJob` por el mismo camino
  (`submit_blit_job`). El descriptor cubre copias **con módulos** (`CopyRect` con
  `words_per_row`/`height`/`source_modulo_bytes`/`destination_modulo_bytes`), BOBs, líneas, C2P…
  Con él se implementan el **borde de scroll** (`CopyRect 20×256`, `mods = 2`, desplaza la
  pantalla una columna) y el **parcheo de copperlist** (`CopyRect 1×N`, `dst_mod = 2`, escribe
  los data words de MOVEs consecutivos). Así no hacen falta firmas propias ni punteros crudos:
  todo se describe con `BlitJob`. Ver `demos/amiga/210_copper_blitter`.
- El chip expone `BBUSY` (`DMACONR` bit 14) → `backend.blitter_busy()`; el Copper **no** lo
  consulta, así que la coordinación es responsabilidad del software.
