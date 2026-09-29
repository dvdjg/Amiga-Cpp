# API de Blitter por intención (cola asíncrona)

API **por intención**: el desarrollador **declara qué quiere** (`fill`, `stamp`, `masked_stamp`) y
**no espera** a que se dibuje. Las peticiones se **encolan** y se ejecutan **cuando el Blitter está
libre**, de una en una (el Blitter tiene un solo juego de registros). Solo `flush()`/`wait()` son
puntos de bloqueo explícitos (como `glFinish`). Referencias:
`docs/reference/amiga/techniques/blitter-cpu-interleaving.md`, [`INTENT_PLANNER.md`](INTENT_PLANNER.md).

## 1. Por qué

Programar un blit y **esperar** (`wait_blitter` sondea `BBUSY`) es coste puro de CPU. El patrón
"cola + feeder" **elimina la espera activa**: la CPU sigue trabajando y el feeder programa el
siguiente job en los huecos. El desarrollador solo marca los **puntos de dependencia** donde de
verdad necesita el resultado.

## 2. Piezas (estado vigente)

```
  intención (sin hardware)          cola (mecanismo genérico)        ejecución (vías)
  ┌───────────────────────┐        ┌────────────────────────┐      ┌──────────────────────┐
  │ q.fill(dst, rect)     │──────► │ IntentQueue<N, BlitOp> │────► │ PlanExecutor         │
  │ q.stamp(src, dst, r)  │        │  FIFO, capacidad N     │      │  → FramePlan         │
  │ q.masked_stamp(...)   │        │  (eng/core/util)       │      │ CopperBlitterExecutor│
  │ q.all(span<BlitOp>)   │        └────────────────────────┘      │  → copperlist        │
  └───────────────────────┘                                       └──────────────────────┘
```

1. **`BlitOp`** (`graphics/blit_queue.hpp`) — una petición (valor pequeño, copiable):
   - `Kind { Fill, Stamp, MaskedStamp }` — limpia (`D=0`), OR de imagen (`D=A|B`), cookie-cut
     (`D=(A&B)|(~A&C)`).
   - `dst`: `BitmapView<PlaneTag, MemoryKind::Chip>` — zona destino (playfield).
   - `src`: `BitmapView<BobTag, MemoryKind::Chip>` (imagen); `mask`: ídem (cookie-cut).
   - `rect`: `eng::Box` (rectángulo en la zona destino); `ashift`: desplazamiento fino (0..15).
   - **No nombra registros.** El **tag** distingue papel (dst `PlaneTag` ≠ src `BobTag`): pasarlos
     intercambiados **no compila**. El **banco Chip** en el tipo impide pasar memoria no-DMA.
2. **`BlitQueue<N, Executor>`** (`graphics/blit_queue.hpp`) — es una `IntentQueue` genérica
   (`eng/core/util/intent_queue.hpp`) con `Item = BlitOp` y `Done = NoDone`. Métodos de intención
   (`fill`/`stamp`/`masked_stamp`) + `all(Span<const BlitOp>)`. **Sin heap**, capacidad fija.
   `enqueue` (no espera) / `flush` (avanza sin esperar) / `wait` (único bloqueo) / `wait_all`.
3. **La vía de ejecución** (`Executor` con `{ bool ready(); void run(const BlitOp&); }`):
   - **`PlanExecutor<Sink>`** — vuelca cada `BlitOp` al **`FramePlan`** vía `blit_job_from` (el
     sumidero único del frame; orden/presupuesto/ejecución siguen en el plan). Es la vía normal.
   - **`CopperBlitterExecutor<Sched>`** — emite instrucciones de **Copper** que programan el
     Blitter (Técnica A / `COPCON`+CDANG): `ready()` = siempre; el blit lo arranca el Copper en su
     línea. **El mismo `BlitOp` se ejecuta por CPU/IRQ o por Copper** — es una estrategia, no otra API.
4. **El mecanismo de cola** es genérico y compartido: `eng::IntentQueue<N, Item, Executor, Done>`
   (`eng/core/util`). La usan el blit (`BlitOp`), el dibujo (`DrawIntent`) y el audio
   (`SampleEvent`). **Mismo ticket + misma completación** para todos.

## 3. Dos traducciones, una sola verdad

- **`blit_job_from(const BlitOp&) -> BlitJob`** — el **único** sitio que traduce intención
  (zona + rect) al **trabajo canónico** (`BlitJob`) que describe el `FramePlan`. `Fill`→`ClearRect`,
  `Stamp`→`OrBlob`, `MaskedStamp`→`MaskedBobCookieCut`.
- **`blitter_job_from(const BlitJob&) -> BlitterJob`** — el **único encoder** a registros
  (`BLTCON*`/`BLTxPT`/`BLTSIZE`). Lo usan el backend (CPU/IRQ) y el `CopperBlitterExecutor`.
  La intención **no inventa un «juego de registros» paralelo**.
- `BlitJob`/`BlitterJob` usan **`BlitPtr`** (dirección `Address<Chip>` a words) para fuente,
  destino y máscara: **un solo tipo** para los tres roles (el hardware es simétrico; quién es
  fuente o destino lo decide el campo).

## 4. Uso (sabor)

```cpp
eng::graphics::BlitQueue<64u, eng::graphics::PlanExecutor<FramePlan>> q;
q.bind(exec);                                  // executor que vuelca al FramePlan

void update(...) {
    q.fill(dst, {0, 0, 320, 256});             // intencion: limpiar
    for (const Bob& b : bobs)                  // intencion: estampar
        q.stamp(sheet, dst, {b.x, b.y, 16, 16}, b.shift);
    q.all(more_ops);                           // array de golpe
    q.flush();                                 // empieza a drenar (NO espera)
    // ... CPU: proyectar mientras el Blitter trabaja ...
    q.wait_all();                               // unico punto de espera: commit del frame
}
```

**Agrupar por estado (opt-in).** Antes de ejecutar el `FramePlan`, el llamador puede agrupar los
blits para que los de **mismo estado común del Blitter** queden adyacentes y el backend omita sus
reprogramaciones (`submit_blit_job` cachea el estado común por racha):

```cpp
plan.sort_by_state();          // reordena ESTABLE por (kind/minterm/shift/modulos/layout)
execute_frame_plan(plan);      // encadena rachas -> menos escrituras a registro custom
```

Solo es lícito si el **orden no importa** (jobs con destinos **disjuntos**: tiles, columnas). NO
con `Clear`/`EOR` sobre regiones solapadas. Ver `RASTER.md` §"Prioridades de rendimiento".

## 5. Completación: evento, no callback

Declarar **no bloquea**; cuando una petición llega a su punto se **avisa** por la política `Done`.
En el engine real es un **`Msg` del mini-SO** (`MsgType::IntentDone`) con el **`ticket`**
(`IntentDonePoster`, `eng/os/intent_done.hpp`); el juego lo atiende en su tabla de despacho. `wait()`
sigue siendo el único bloqueo y **solo** si el juego lo pide (`BlitQueue` usa `NoDone`; el aviso
real del planner va por la `DrawQueue`). Ver [`INTENT_PLANNER.md`](INTENT_PLANNER.md) §5.

## 6. Decisiones y límites

- **Orden FIFO** (el Blitter es secuencial); prioridad sería una extensión.
- **Capacidad fija `N`** (potencia de dos); si se llena, `enqueue` avanza la cola (`flush`/`drain`).
- **Los puntos de dependencia son explícitos**: como en GL, la asincronía los **exige**.
- **El bus es compartido** con la CPU y el DMA de display. Con `BLTPRI` (blitter-nasty) el Blitter
  **no cede slots**: el feeder por IRQ **no** garantiza solape con la CPU (medido). Por eso el
  **poll** es el modo por defecto (usa los huecos que ya existen, sin IRQ).

## 7. Procedencia (compilador) vs vida (RAII)

- **La PROCEDENCIA la caza el tipo.** `Address<Chip>`/`ChipView<Tag>`/`BitmapView<Tag,Chip>` **solo**
  nacen de fuentes Chip (`MemBank<Chip>`, `Block<Tag,Chip>`, `gfx::Bitmap`, `.MEMF_CHIP`,
  `ChipStorage`/`INCBIN_CHIP`). Un búfer de **pila** o un `static` en **Fast no** compilan
  (salvo `from_storage`, frontera explícita, **prohibida fuera del backend**; ver
  `tools/check/casts-frontier.txt`).
- **La VIDA no la puede cazar el tipo.** "Búfer de pila dentro del ámbito" vs "…que se sale" es
  *lifetime*, como en `Span`: responsabilidad del desarrollador. La mitigación: el **dueño** es un
  `Block<Tag, Bank>` (**movible, no copiable**: un solo dueño) del `MemBank` (pool fijo, **sin
  heap**); las APIs toman **vistas** no propietarias. Para datos Chip **estáticos**: `ChipStorage`.
  Ver `MEMORY_OWNERSHIP.md` (contrato del developer) y `ROADMAP_MEMORY_OWNERSHIP.md`.

## 8. Relación con lo que ya existe

- `FramePlan` es **el sumidero** de la capa 2: la `BlitQueue` es el **front-end de intención**; su
  `PlanExecutor` añade `blit_job_from(op)` al plan. **Un solo dueño** del orden/presupuesto/ejecución.
- El backend (`amiga_blitter.cpp`) ejecuta el plan con `blitter_job_from` (registros) — no se
  reescribe.
- El **planner** (capa de arriba) añade vocabulario de juego (`DrawIntent`) y **completación**; ver
  [`INTENT_PLANNER.md`](INTENT_PLANNER.md). Estado probado: HOST-365 (equivalencia intención↔directo),
  HOST-368 (cola + receta + capa), HOST-369 (intención → evento).
