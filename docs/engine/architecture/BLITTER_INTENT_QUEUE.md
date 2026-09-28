# API de Blitter por intención (cola asíncrona)

API **adicional** (no sustituye al `FramePlan`/`OrBlobBatch` actuales) inspirada en OpenGL: el
desarrollador **declara intenciones** (`stamp`, `fill`, `copy`…) y **no espera** a que se dibujen.
Las peticiones se **encolan** y se ejecutan **cuando el Blitter está libre**, de una en una o como
un **array de golpe**, avanzando por la **IRQ de blit** (o por poll en los huecos). Ver
`docs/reference/amiga/techniques/blitter-cpu-interleaving.md` y `BACKGROUND_TASKS.md`.

## 1. Por qué

Hoy cada blit se programa y **se espera** (`wait_blitter` sondea `BBUSY`): en la 117 eso es **~30 %
del tiempo de CPU**, y el comentario de `DrawObject` del original ya intenta solaparlo a mano (calcular
el vértice N+1 mientras corre el blit N). El patrón "cola + IRQ" lo generaliza: **la espera
desaparece** y el desarrollador solo marca **puntos de dependencia** explícitos.

## 2. Piezas

```
   intencion (sin hardware)        cola (ring fijo, sin heap)      ejecucion
   ┌───────────────────────┐      ┌────────────────────────┐      ┌────────────────────┐
   │ q.stamp(sheet,f,x,y)  │─────►│ [ BlitOp ][ BlitOp ]...│─────►│ feeder: IRQ blit o │
   │ q.fill(screen,rect,c) │      │  FIFO, capacidad N     │      │ poll en los huecos │
   │ q.all(span<BlitOp>)   │      └────────────────────────┘      └─────────┬──────────┘
   └───────────────────────┘                                               │ programa BLTxPT/BLTSIZE
                                                              q.flush() empezar / q.wait() vaciar
```

1. **`BlitOp`** — una petición (variante ligera, sin punteros a función):
   `Kind { Stamp, Fill, Copy, Line }` + params del dominio (`BitmapView src/dst`, `Rect`, `rgb444`,
   `sheet frame`). **No nombra registros.**
2. **`BlitQueue<N>`** — ring de `BlitOp` (capacidad fija). Métodos de **intención** (`stamp`,
   `fill`, `copy`, `line`) + `all(eng::Span<const BlitOp>)` para **un array de golpe**. Sin heap.
3. **El feeder** — quién programa el Blitter cuando queda libre:
   - **(P) poll**: se llama `pump()` en los puntos donde el CPU ya espera (la espera de VBlank y el
     hook de espera de Blitter, `g_blitter_service`). **Coste cero de IRQ.**
   - **(I) IRQ**: se habilita la **IRQ de blit** (`INTENA`/`INTREQ` bit 6, nivel 3, ya cableada en
     `level3_dispatch`); al terminar un job, el handler programa el siguiente. **Coste ~250 c/job**,
     pero el CPU queda **libre** entre blits para otra cosa.
   - **Política configurable** (`Feed::Poll | Feed::Irq`).
4. **Sincronización explícita** (lo único que bloquea):
   - `flush()` — garantiza que se está drenando (no vacía).
   - `wait()` — vacía la cola: el **punto de dependencia** (leer el destino, reusar el buffer,
     cambiar de pantalla). Es el `glFinish`.

## 3. Ejecución (restricciones reales)

- El Blitter tiene **un solo juego de registros** → la cola se **serializa** (de a uno). El feeder no
  paraleliza blits; solo elimina la espera **activa**.
- El bus es compartido con la CPU y el DMA de display. Con `BLTPRI` (blitter-nasty) el Blitter no
  cede slots: el feeder por IRQ **no** garantiza solape con la CPU (medido: el pase fusionado no
  ayudó). Con `BLTPRI` off el Blitter es más lento pero la CPU avanza. **Por eso el poll es el
  defecto** (no añade IRQs y usa huecos que ya existían) y la IRQ es opt-in para "fire-and-forget".

## 4. Uso (sabor)

```cpp
eng::gfx::BlitQueue<64> q;                       // se ata al backend una vez

void update(...) {
    q.fill(screen, {0, 0, 320, 256}, bg);        // intencion: limpiar
    for (const Bob& b : bobs)                    // intencion: estampar
        q.stamp(sheet, b.frame, b.x, b.y);
    q.all(more_ops);                             // array de golpe
    q.flush();                                   // empieza a drenar (NO espera)
    // ... CPU: proyectar el siguiente frame mientras el Blitter trabaja ...
    q.wait();                                    // unico punto de espera: commit de este frame
    install_copper(active);                      // el buffer destino ya esta completo
}
```

## 5. Relación con lo que YA existe

- `submit_blit_job`/`blitter_submit` programan registros → el feeder los usa (no se reescribe el
  backend).
- `g_blit_task` + `level3_dispatch` bit 6 → **el punto de entrada de la IRQ ya está**; falta el
  handler que avanza la cola.
- `g_blitter_service` (`wait_blitter` drena el fondo) → el **poll** se cuelga de ahí.
- `OrBlobBatch` (batch sincrono actual) → un caso particular (`stamp` × N + `wait` al final).
- `FramePlan` → puede **producir** `BlitOp`s en vez de ejecutarlos él mismo.

## 6. Decisiones y límites

- **Orden FIFO** (el Blitter es secuencial); prioridad opcional como extensión.
- **Capacidad fija** `N` (sin heap); si se llena, `flush`+`wait` antes de encolar (política).
- **Los puntos de dependencia son explícitos**: como en GL, la asincronía **exige** declararlos; el
  `wait()` es el único bloqueo.
- **Coherencia con `wait_blitter`**: la cola debe respetar los blits ya en vuelo (un `flush` tras un
  blit suelto).

## 7. Refinamientos (memoria, zonas, Copper y procedencia)

### 7.1 `src`/`dst` con **tag de Chip** (no `u8*`)

El engine ya tiene direcciones **etiquetadas**: `Address<MemoryKind::Chip>` / `ChipView<Tag>` /
`Block<Tag, Chip>` (`core/types/typed.hpp`, `core/types/memory_kind.hpp`). El `BlitOp` **no** lleva
`u8*` crudos sino la **vista etiquetada**:

```cpp
struct BlitOp {
    BitmapView<PlaneTag, Chip> dst {};  // zona destino (playfield): banco + geometría en el tipo
    BitmapView<BobTag, Chip> src {};    // zona origen (atlas/asset): BobTag ≠ PlaneTag
    // + words/height/módulos/ashift (rectángulo concreto dentro de la zona)
    ...
};
```

El ejecutor convierte a `u8*` **en el `submit`** (la única frontera). Pasar un array de pila o un
`static` en Fast **no compila** sin un `Address<K>::from_storage` explícito — prohibido fuera del
backend (CODING_STYLE §232). **El tag caza el error en compilación**: el destino es una zona
`PlaneTag` y el origen una `BobTag`, de modo que **intercambiarlos no compila** (probado en
HOST-365). La geometría de la zona (base + `row_bytes`/`plane_count`/`layout`/`plane_step`) viaja en
la vista; los registros concretos (`words`/`height`/módulos) aún van explícitos en la op (una pasada
rect-based, que los derive de la zona + un `Rect`, está en `ROADMAP_TIPOS_Y_CASTS.md` §6).

### 7.2 Zona rectangular: `BitmapView<Tag, K>` (usar `Bitmap`)

Ya existe `eng::graphics::Bitmap` (memoria + geometría + layout + addressing: `width/height/planes`,
`PlaneLayout{Interleaved,Separate}`, `row_bytes`, `alignment`). La **vista** no propietaria
`BitmapView<Tag, K>` (en `eng/graphics/bitmap_view.hpp`) ya **existe** — `width`/`height`/`row_bytes`/
`plane_count`/`layout`/`plane_step` + la `MemView<Tag, K>` (banco) — y **unifica** a los consumidores:
**sprites hw, BOBs, zonas del framebuffer/pantalla, viewports**. El `Bitmap` (o un `Block`) es el
**dueño**; la vista es lo que viaja por las APIs (como `Span`, pero de **zona gráfica**). `BobTarget`
es hoy un alias: `BitmapView<PlaneTag, MemoryKind::Chip>`.

El **tag** elige el camino: un `BitmapView<ChunkyTag, Fast>` **no** se puede pasar donde se exige
Chip; para copiar **chunky-Fast → planar-Chip**, el **C2P detecta los tags** (origen no-Chip, destino
Chip) y usa la **ruta CPU** en vez del Blitter. Los tags **eligen el método**.

### 7.3 Efectos de Copper (offsets/módulos por línea) — capa aparte

La **estructura base** (`BitmapView`) queda **plana** (memoria+geometría). El acoplamiento al Copper
vive en una **capa superior**: una **intención** ("esta zona se muestra con el puntero de plano X, el
módulo M y parcheo por línea") que un **materializador** traduce a la **copperlist** en el commit.
El engine ya tiene las piezas: `copper::Plan`/`Scheduler` + el **parcheo de palabras** (`slot_word`)
del trabajo de la 213. Diseño: `BitmapView` (base) **+** `CopperIntent` (superior), separados.

### 7.4 Programar el Blitter **por Copper**: ya es una estrategia

El `BlitQueue<N, Executor>` inyecta el ejecutor por `concept`. Un **`CopperBlitterExecutor`**
implementa `submit(op)` **emitiendo instrucciones de Copper** (con `COPCON`/CDANG — la "Técnica A"
documentada) en vez de programar registros: `blitter_free()` = siempre `true` (lo hará el Copper en la
línea) y `wait()` = el punto del raster (el commit). **El mismo `BlitOp` se ejecuta por CPU/IRQ o por
Copper** — es una estrategia, no un API distinto. **Sí hay compatibilidad futura.**

### 7.5 Procedencia (compilador) vs vida (RAII) — el agujero del heap-less

- **La PROCEDENCIA la caza el tipo.** `Address<Chip>`/`ChipView<Tag>` **solo** nacen de fuentes Chip:
  un `MemBank<MemoryKind::Chip>`, un `Block<Tag, Chip>`, un `gfx::Bitmap` (siempre Chip), un búfer
  `.MEMF_CHIP`, o `ChipStorage<Tag, N>` / `INCBIN_CHIP`. Un búfer de **pila** o un `static` en **Fast
  no** son `Address<Chip>` → **no compila** pasarlos (salvo `from_storage`, prohibido fuera del
  backend). **El compilador sí lo caza** (esto es §232). Aplica igual a Paula, Blitter y Copper: esos
  APIs deben tomar **tipos etiquetados**.
- **Lo que el tipo NO puede: la VIDA.** Distinguir "búfer de pila en Chip **dentro** del ámbito" de
  "…que se **sale**" es **lifetime**, no memoria. Es el mismo problema de `Span`: no propietario =
  responsabilidad del desarrollador. C++23 no lo deduce en general.
- **La solución correcta (RAII) ya existe a medias:** que el API tome **vistas etiquetadas**
  (`BitmapView<Tag, Chip>`) y que la **dueña** sea un **`Block<Tag, Chip>`** (RAII) que devuelve al
  **`MemBank<Chip>`** en su destructor. Así: (a) el tipo garantiza Chip, (b) el RAII garantiza la
  liberación, (c) **sin heap** — el `MemBank` es un **pool fijo**, no `malloc`. "Romper el no-heap" =
  usar el pool tipado; el "template auxiliar" **es** `Block`/`MemView`.
- **Estáticos:** para datos Chip **estáticos**, no usar un `static` suelto (su sección es incierta):
  usar **`ChipStorage<Tag, N>`** (coloca en `.MEMF_CHIP` por tipo) o `INCBIN_CHIP` → sin cast y sin
  duda de procedencia.
