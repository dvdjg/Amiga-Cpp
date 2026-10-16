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
- **`blob_run_from`/`blob_run_continues`** (`graphics/blitter_state.hpp`) — la **clasificación de
  racha**: qué `BlitJob` ejecuta con lote de estado fijo (`BlobOp` + `BLTSIZE` + módulos) y hasta
  dónde llega la racha homogénea. La comparten el ejecutor síncrono y el feeder async (HOST-451).
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

**Medición (demo `211_blit_state_bench`):** rejilla de 112 tiles disjuntos con dos estados
intercalados. Agrupar no cambia los lanzamientos (`blitter_starts = 448`) ni el bitmap (capturas
byte-idénticas) y sube los aciertos de la caché de racha (`blitter_common_hits`) de **6 a 110**;
el control homogéneo ya está saturado (112/112), así que el orden agrupado solo aporta en escenas
**heterogéneas**.

## 5. Completación: evento, no callback

Declarar **no bloquea**; cuando una petición llega a su punto se **avisa** por la política `Done`.
En el engine real es un **`Msg` del mini-SO** (`MsgType::IntentDone`) con el **`ticket`**
(`IntentDonePoster`, `eng/os/intent_done.hpp`); el juego lo atiende en su tabla de despacho. `wait()`
sigue siendo el único bloqueo y **solo** si el juego lo pide (`BlitQueue` usa `NoDone`; el aviso
real del planner va por la `DrawQueue`). Ver [`INTENT_PLANNER.md`](INTENT_PLANNER.md) §5.

## 6. Feeder: liberar la CPU del bucle (estado real)

El objetivo asíncrono es **no bloquear el bucle principal**: la CPU sigue con su pila y contexto, y
el avance del Blitter ocurre en el **hueco de VBlank** (poll) o en la **IRQ de blit** (nivel 3, bit
6, `level3_dispatch` → `g_blit_task`, ya cableada). Dos vías:

- **(P) Poll — por defecto.** La cola avanza en el punto donde la CPU **ya** espera (VBlank, `wait_blitter`
  del commit). El bucle **no se bloquea** salvo por `wait_all()` explícito. **Coste de IRQ cero.**
  Es el default medido (con `BLTPRI`/blitter-nasty el Blitter no cede slots y el poll va igual o
  mejor). El `FramePlan` se ejecuta de golpe en el commit; la CPU queda libre **entre** frames.
- **(I) IRQ de blit — opt-in.** El gancho (`set_blit_service` + `level3_dispatch` bit 6) permite
  conectar un **feeder** que, al terminar un job, programe el siguiente; la receta ejecutable está
  en `demos/techniques/amiga/blitter/212_blitter_feeder`. Aviso: en OCS el
  Blitter **no encadena solo** — alguien (CPU en la ISR, o **Copper**) debe escribir el próximo
  `BLTSIZE`. La vía "sin CPU" real es el **Copper** (`CopperBlitterExecutor`, Técnica A): el Copper
  escribe los registros en su línea. La ISR, si se usa, interrumpe brevemente y **no** altera la
  labor de fondo (transparente).

**Rachas homogéneas: el plan emite el lote.** El camino `inline` (`OrBlobBatch`/`BlobBatch`) y la
llamada de backend (`submit_blit_job`) son **dos vías del mismo estado** (una para lotes homogéneos,
otra para el `FramePlan` heterogéneo). La unificación está hecha: la clasificación de rachas vive en
el dominio (`graphics::blob_run_from`/`blob_run_continues`/`blob_run_same`, `blitter_state.hpp`,
HOST-451) y la usan **las dos vías de ejecución del plan** — síncrona y async:

- **`execute_frame_plan` (CPU)**: agrupa los jobs consecutivos que comparten el lote de estado fijo
  (`BlobOp` + `BLTSIZE` + módulos; cookie-cut `$CA`, clear `$00` u OR `$FC` interleaved de un canal)
  y los ejecuta con `BlobBatch` (estado programado una vez; por objeto, punteros + `shift` +
  `BLTSIZE`). El resto de jobs van por el encoder genérico.
- **Feeder async (IRQ)**: al cruzar una racha, `frame_plan_async_step` arranca el lote
  (`begin` + primer objeto) y cada IRQ de fin de blit escribe **solo el objeto siguiente**
  (`one()`), sin re-codificar registros en la ISR. Contador de diagnóstico:
  `AmigaBackend::blit_async_runs()`.
- **La caché de estado común de `submit_blit_job` está desactivada por job a propósito** (bug 1 de
  066: sus valores pueden ser de otro dueño — el SO comparte el Blitter; ver
  `docs/debugging/investigaciones/066-async-blit-irq-bajo-el-so.md`). Por eso la única vía para
  omitir las ~8 escrituras de estado por objeto es ejecutar una **racha** (`BlobBatch`), no un job
  suelto.
- **Equivalencia**: una racha escribe por objeto los **mismos registros efectivos** que el encoder
  (HOST-451, incluidas las ventanas `AFWM/ALWM` del OR); en emulador, los frames deterministas de
  la 213 (`f0020..f0024`, plan síncrono) son **byte-idénticos** (SHA256) antes y después del cambio.

### 6.1 Cuánto ahorra el modo asíncrono (medido, demo 213)

El async **ahorra porque elimina la espera activa del 68000**, no porque haga los blits más rápidos:
el Blitter sigue teniendo un solo juego de registros y los blits de una racha se serializan entre sí.
Lo que desaparece del hilo principal es el sondeo de `BBUSY`.

Medición (2026-10, A500, demo 213 = 16 BOBs cookie-cut + clear de banda + copia de fondo, 5 planos):

- El frame del `render` consume ~392 000 ciclos (~2,8 campos de 141 876 ciclos); la sección
  `present` (las 17 esperas de blit + programación) es ~154 000 ciclos, ≈ **40 % del render y ~1,1
  campos** de espera activa pura.
- Con el ejecutor por lotes (`BlobBatch`) la 213 subió de 22,7 a 25 fps: se recuperó el coste de
  re-programar registros por job, pero **no** las esperas (que siguen, una por objeto).
- **Vía IRQ cableada en la 213** (`App::set_async_present(true)`, `execute_frame_plan_async`): la
  cadena completa (17 jobs heterogéneos: copia + clear + BOBs) se lanza en el `present()` y la **IRQ
  de fin de blit** programa el resto. Instrumentado con contadores en la ISR
  (`g_blit_async_launches/steps/ends`): ~25 lanzamientos y ~16 submits por frame, cierres de cadena
  coherentes; sin la espera del `wait_blitter` en el hilo principal.

Resultados del A/B (A500, demo 213 completa, contador de ciclos del emulador; `measure-fps`). Los
valores son **tras** aplicar el camino de frame a coste cero (`ZERO_COST_FRAME_PATH.md`), que bajó el
CPU del frame y llevó al síncrono a 1 campo:

| Config | fps | campos/frame |
|---|---|---|
| debug síncrono | 32,62 | 1,53 |
| debug asíncrono | 28,07 | 1,78 |
| release síncrono | **49,92** | **1,00** |
| release asíncrono | 33,53 | 1,49 |

Antes de esas optimizaciones (solo el ejecutor por lotes): debug 24,96 / 22,51 y release 28,55 /
25,21. La diferencia entre modo síncrono y asíncrono se explica por el **uso del bus**, no por el
CPU: ver las claves.

Claves del A/B:

- **El async elimina la espera del hilo principal** (sección `present` de ~130 k a ~4 k ciclos) y
  encadena cadenas **heterogéneas** sin código extra: el feeder usa `blitter_submit` por job
  (encoder genérico) y, en rachas homogéneas, la ejecución por lote (`blit_async_runs()` ≥ 1 por
  frame en la 213; `blit_async_ends()` == frames y `chain_done` ≈ frames con los avisos).
- **El async no acelera el frame si el trabajo de CPU es gordo**: al solapar CPU y Blitter, el **bus
  compartido** hace que el 68000 pierda slots y el `render` se infle (~153 k → ~700 k ciclos en
  `-O0`). El resultado neto empeora (24,96 → 22,51 fps en debug; 28,55 → 25,21 en release). El solape
  solo gana cuando el trabajo de CPU **cabe en el hueco** del DMA. En esta demo (CPU y Blitter
  comparables, A500 sin Fast RAM) el síncrono gana; el async queda como **opt-in** para escenas con
  mucha CPU-pesada entre frames o cuando el siguiente job lo dispara el **Copper** (sin competir).
- **Corrección de ciclo de vida**: la cadena retiene el `FramePlan`; el motor espera su fin y hace el
  `commit` al **inicio del frame siguiente** (`App::begin_async_frame`), de modo que el frame mostrado
  está completo (sin tearing) y el plan no se reutiliza con la cadena viva. En la 213 (bitmap único,
  sin swap) una captura a mitad de cadena puede verse parcial: es la naturaleza del dibujado
  asíncrono sobre un solo buffer, no un fallo de la cadena.
- **El slot de la IRQ de blit es exclusivo**: el bucle por defecto instala el servicio de fondo
  (`BackgroundBlitterService`, `engine.hpp`) y, si no se libera, el modo async **falla en silencio**
  y cae al camino síncrono. `App::set_async_present(true)` desactiva ese servicio del engine
  (`Engine::set_blit_service_enabled(false)`).

**Avisos de la cadena (tickets).** El plan admite **marcas de aviso** (`FramePlan::add_notify`,
expuestas por `Screen::notify`): cada una lleva un `ticket` y dispara cuando han terminado los
trabajos encolados **hasta su declaración**. Se publican como `MsgType::IntentDone` con el `ticket`
en `payload.user.a`, y el juego los drena en su `update`; el contrato no depende del modo de
present: en **async** los dispara la IRQ de fin de blit al cruzar el punto, y en **síncrono** los
publica el `present` al cerrar la cadena (el `execute_frame_plan` espera al último blit). Un aviso al
final de la ristra se declara tras el último `sprite`/`clear_box`:

```cpp
auto s = app.screen();
s.clear_box(band);
for (const Bob& b : bobs) s.sprite(sheet, b.x, b.y, b.frame);
s.notify(kFrameDone);          // aviso con Id: "terminó toda la ristra"
app.present();                 // async: la IRQ encadena y publica IntentDone al llegar
```

Verificado de punta a punta en la 213 (`A500_k_async_present1_*`): un aviso por frame, contado en
`update` (`IntentDone` con `kChainTicket`), ≈ frames mostrados. Con `ReorderPolicy::GroupByState` los
avisos inmovilizan el orden (la agrupación no debe mover sus puntos).

**A/B de las rachas (2026-10-16, A500, 213 con `-DK_STREAM_BOBS=0`).** `blob_run_from` compartido en
ambas vías (mismo binario reconstruido con/sin el cambio):

| Config (debug) | antes | después |
|---|---|---|
| `K_ASYNC_PRESENT=1` (plan + IRQ) | 26,53 fps (1,885 campos) | **27,58 fps (1,813 campos)** |
| `K_ASYNC_PRESENT=0` (plan + CPU) | 33,05 fps / 214 656 ciclos-f | 32,2–32,3 fps / 219 500 ciclos-f |

Lectura honesta: el **async mejora ~4 %** (menos escrituras de registros por objeto dentro de la
ISR). En el síncrono el perfil (`profile.mjs`) da **+0,4 % de ciclos ocupados** (y +0,47 k en una
sección cuyo código no cambió: parte es colocación de código), mientras que `measure-fps` muestra
+2,4 % de ciclos **de pared**: la demo está **justo en el umbral de 1,5 campos** (1,513 → 1,55), así
que el delta de pared lo domina la cuantización de VBlank, no el trabajo por frame. La equivalencia
visual en esa vía es **byte a byte** (SHA256 de `f0020..f0024` idénticos antes/después).

Implicación para el modo asíncrono: es el **método opt-in de la fachada** con un contrato claro
(cadena transparente + avisos por Id). El síncrono sigue siendo el defecto de baja latencia cuando el
frame ya cabe; el async se elige por **presupuesto de CPU** o para cadenas largas con CPU-pesada
detrás.


## 7. Decisiones y límites

- **Orden FIFO** (el Blitter es secuencial); prioridad sería una extensión.
- **Capacidad fija `N`** (potencia de dos); si se llena, `enqueue` avanza la cola (`flush`/`drain`).
- **Los puntos de dependencia son explícitos**: como en GL, la asincronía los **exige**.
- **El bus es compartido** con la CPU y el DMA de display. Con `BLTPRI` (blitter-nasty) el Blitter
  **no cede slots**: el feeder por IRQ **no** garantiza solape con la CPU (medido). Por eso el
  **poll** es el modo por defecto (usa los huecos que ya existen, sin IRQ).

## 8. Procedencia (compilador) vs vida (RAII)

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
- Para origen de `AssetCache`, obtener una `AssetDmaLease` (solo admite `MemoryKind::Chip`) antes de
  construir el `BitmapView`, conservarla hasta que el `FramePlan`/cola haya terminado el último job y
  liberarla entonces. La lease pertenece al llamador y cubre todos los jobs que comparten el asset.

## 9. Relación con lo que ya existe

- `FramePlan` es **el sumidero** de la capa 2: la `BlitQueue` es el **front-end de intención**; su
  `PlanExecutor` añade `blit_job_from(op)` al plan. **Un solo dueño** del orden/presupuesto/ejecución.
- El backend (`amiga_blitter.cpp`) ejecuta el plan con `blitter_job_from` (registros) — no se
  reescribe.
- El **planner** (capa de arriba) añade vocabulario de juego (`DrawIntent`) y **completación**; ver
  [`INTENT_PLANNER.md`](INTENT_PLANNER.md). Estado probado: HOST-365 (equivalencia intención↔directo),
  HOST-368 (cola + receta + capa), HOST-369 (intención → evento).

## 10. La GUI en el modelo asíncrono (no adoptado)

La UI (`eng::ui`: `UiPainter`/`draw_tree`) dibuja hoy **síncrona** por el `RectFillSink`/`Surface`:
cada `fill_rect` se ejecuta ya (Blitter + `wait_blitter`, con cookie-cut `$CA` también para el borde
parcial). Es simple y correcto, pero **no solapa CPU y Blitter**.

**Cómo sería** adoptar el modelo asíncrono en la UI sin cambiar la API de dibujo:

1. `UiPainter::fill`/`frame`/`bevel`/`text` **encolan** (no ejecutan): un `BlitOp` (`Fill`) / `Line`
   en la `BlitQueue` del frame (o directamente en el `FramePlan`), igual que `Screen::fill_box`.
2. El plan/cola se ejecuta **una vez** en `present()` (`execute_frame_plan` /
   `execute_frame_plan_async`): el Blitter encadena los rellenos sin que la CPU espere entre ellos.
3. `wait_blitter` solo en los **puntos de dependencia** reales (p. ej. si un `draw_text` por CPU
   debe escribir sobre un relleno del Blitter, o al cerrar el frame).

Requisitos: los rellenos de UI **no** deben leer su propio resultado a mitad de frame (hoy no lo
hacen), y el borde parcial ya está resuelto en el `FillRect` asíncrono (cookie-cut `$CA`, ver
[`blitter-fill-constant.md`](../../reference/amiga/techniques/blitter-fill-constant.md)), así que los
widgets no alineados a 16 px no se recortan.

**Beneficio escaso en un juego.** La UI emite **pocas** primitivas por frame (un panel, unos
widgets: decenas) y muchas se repintan **por zona** (solo slider/estado), así que el ahorro de no
esperar al Blitter es pequeño frente al trabajo pesado del frame (tiles, BOBs, Copper), que ya va
por el plan asíncrono. Por eso **no se adopta ahora**: la complejidad añadida (dependencias
explícitas, orden) no compensa. Tendría sentido si la UI creciera mucho (formularios grandes) o si
la medición mostrara un cuello de botella de espera en `fill_rect`; la vía síncrona queda como
respaldo y para `init`/cambios inmediatos.
