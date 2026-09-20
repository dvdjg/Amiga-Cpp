# Composición de escenas: el modelo de tres planos

Este documento fija cómo el engine describe y gestiona **escenas** (configuraciones del
hardware Amiga) de forma versátil, sustituyendo la familia de "drivers" por efecto. Nace del
problema observado al importar efectos de `demoscene-repo-orig`: cada efecto traía su propia
configuración cableada como una clase (`PlanarScene`, `StaticEhbScene`, `CopperChunkyScene`,
`CanvasScene`), con tres consecuencias.

## 1. El problema

- **Reutilización forzada**: una configuración creada para un efecto se acaba usando para
  otra cosa, ignorando lo que no aplica (`PlanarScene` usado como display planar plano con
  `row_repeat = 1`, sin `bplcon1_shift`).
- **Interfaces arbitrarias**: cada driver expone métodos elegidos a dedo; no hay un contrato
  común más allá del `concept` `GraphicsDriver` (`bind` + `bitplane_bytes_for` + `takeover`/
  `install`).
- **Explosión combinatoria**: importar 50 efectos podría significar decenas de clases casi
  iguales, inmanejable.

Y, sin embargo, cada aplicación necesita **configurar el hardware con precisión quirúrgica**
y **cambiarlo en runtime** (un juego que usa un efecto por pantalla).

## 2. El modelo: tres planos separados

Una escena se describe en **tres planos** ortogonales, cada uno con su dueño:

```text
   RECURSOS                 PROGRAMA                     COMPORTAMIENTO
   (qué hace falta)         (timeline de registros)      (CPU + eventos)
   ┌────────────────┐       ┌────────────────────┐       ┌──────────────────┐
   │ bitplanes      │       │ lista de Copper     │       │ setup / teardown │
   │ layout/geom.   │       │ (por scanline)      │       │ tarea por frame  │
   │ paleta/sprites │       │ + puntos de parcheo │       │ IRQ (VBlank/blit)│
   │ buffers (N)    │       │ + presupuesto       │       │ patch handles    │
   └────────────────┘       └────────────────────┘       └──────────────────┘
        POD, fijo                data (Copper)               código + datos
```

- **Recursos**: memoria Chip y registros que la escena ocupa (geometría, planos, paleta,
  sprites, nº de buffers). Estructura POD de capacidad fija (sin heap).
- **Programa**: el **timeline de registros por scanline**. Es *data* ejecutada por el
  **Copper** (ver §3). Lo emite el `copper::Scheduler`/`copper::Plan`; la escena solo lo
  construye (una vez o por frame) y lo **parchea**.
- **Comportamiento**: lo que **no** es Copper (C2P, rellenos, transforms, blits) y los
  **eventos** del ciclo de vida. Tareas sin virtuales (`function_ref` o functors), con
  buffers del llamador.

## 3. El Copper es el intérprete: el coste de "data-driven" es casi nulo

La objeción al enfoque data-driven es el coste de interpretar en runtime. **En Amiga no
aplica si el artefacto es una lista de Copper**: el chip la ejecuta por DMA (coste CPU ≈ 0) y
la CPU solo **parchea** valores. Por eso este diseño separa lo *estructural* (palabras de
Copper, que se construyen una vez) de lo *variable* (handles de parcheo, que se escriben por
frame).

La regla: **si la descripción es estructural (cerca del registro) es data; si es semántica
("scroll de playfield") hay que traducirla cada frame y se paga**. El engine elige
estructural, con bloques de alto nivel que **expanden en tiempo de construcción**, no en el
hot path.

## 4. Composición: etapas, presets, handles y tareas

Las piezas se unen por **composición de etapas** (builder plano; el *expression template*
es una optimización posterior, §5), y cada escena concreta pasa a ser un **preset** (una
función), no una clase.

```text
   compose(recursos,
       display(diw, ddf, planos, layout),      // etapa: emite BPLCON0/DIW/DDF/BPLxPT
       palette(base) | zones(sky),             // etapa: paleta base + zonas raster
       row_repeat(4) | bplcon1(0x22) | reverse_ptrs,  // features verticales/orden
       surface(viewport),                       // etapa opcional: expone Playfield/Surface
       on_setup(carga), on_vblank(fill), on_blitter_done(chain), on_teardown(libera),
       patch<u16> scroll_x, patch<Palette> sky) // handles de runtime
```

- Cada **etapa** contiene: (a) lo que **pide** (recursos), (b) lo que **emite** (Copper),
  (c) opcionalmente lo que **ejecuta** (tareas) y lo que **expone** (handles).
- Los **presets** son funciones que devuelven una escena compuesta:
  `ehb_preset(base, zones)`, `ham_preset(...)`, `planar4_preset(...)`. No hay clases por
  efecto.
- Los **handles de parcheo** son el punto de "precisión quirúrgica": la app escribe un
  valor por frame y el engine resuelve qué palabras de Copper tocar (sin interpretar nada).
- Las **tareas** cubren el ciclo de vida (`setup`, por frame antes/después de VBlank,
  IRQ de blitter, `teardown`), con la firma del engine (`function_ref` sin heap).

### Cambio de escena en runtime

Cambiar de efecto = **cambiar el programa/recursos**. Con `MultiBuffered` + `copper::Plan`
ya hay doble buffer de display y de copperlist: una escena viva puede **reconfigurar** su
sub-programa (p. ej. una `ModeSwitchZone`, o reemplazar un track de paleta) sin dejar de
mostrar. Añadir una escena nueva en runtime = construir su programa en el bloque trasero y
publicarlo en el swap, no instanciar una clase.

## 5. Gramática: builder ahora, expression templates después

El primer paso es un **builder plano** (funciones y composición de structs) que emite data y
declara recursos. Es legible, depurable y sin bloat. Sobre él, si hace falta, se puede
construir un **expression template** que exprese la misma composición como *tipo*, con el
compilador plegando etapas y features a código óptimo; **siempre que el artefacto siga
siendo data (Copper)**, no código interpretado. El ET debe reservarse a la **estructura**
(qué etapas, en qué orden, con qué features), y exponer los **valores variables** como
handles, no como nodos del árbol.

Guardarraíl: medir *code bloat* y tiempo de compilación si se usa ET a fondo (ya pesó en
`eng/core/expr.hpp`).

## 6. Guardarraíles

- **Coste hardware visible**: el programa y su `copper::ScheduleReport` (waits/moves por
  línea, overflow) quedan auditables; los presets no lo esconden.
- **Sin heap ni virtuales** en el hot path; capacidades fijas (pools/arenas). El cambio de
  escena en runtime no asigna.
- **Presupuesto** de Copper por scanline (`Plan`) y de Blitter por frame (`FramePlan`/
  `BlitBudget`).
- **Escape hatch crudo** siempre disponible (`raw_copper(...)`: MOVEs sueltos) para efectos
  que no encajen en las etapas.
- **Un solo núcleo, sin familia**: la fontanería (bloques, `bind`, accessors, `install`/
  `takeover`) vive en un único tipo (`Scene`/`HardwareProgram`); los presets la usan por
  composición. **No** hace falta una base CRTP: no hay varias clases hermanas de las que
  extraerla.

## 7. Relación con lo que ya existe

| Plano | Pieza existente | Falta |
|---|---|---|
| Recursos | `Block<Tag>`, `MemorySystem`, `MultiBuffered` | un `SceneResources` POD que agrupe geometría/layout/buffers |
| Programa | `copper::Scheduler`, `copper::Plan`, `CopperIntent`, `ModeSwitchZone` | un `HardwareProgram` que agrupe la lista + recursos + presupuesto |
| Comportamiento | `FramePlan` (blits), `function_ref`, IRQ/VBlank | tareas de ciclo de vida homogéneas y handles de parcheo tipados |
| Composición | — | `compose(...)` + etapas + presets |

## 8. Migración

1. `StaticEhbScene` → `ehb_preset(base, zones)` (añadir zonas de paleta a las etapas).
2. `PlanarScene` → `planar_preset(...)` + etapas `row_repeat`/`bplcon1`/`reverse_ptrs`.
3. `CanvasScene` → etapa `surface(...)` (enlaza `CanvasPlayfield` sobre los planos).
4. `CopperChunkyScene` → etapa `copper_chunky(...)` (lista propia, sin bitplanes).
5. Demos/tests migran a los presets; se conservan las clases como `using`/shim hasta cerrar.

## 9. Verificación

- Test host por etapa (emisión de Copper esperada) y del compuesto (orden/presupuesto).
- Test host de los handles de parcheo (escribir un handle cambia la palabra correcta).
- Gate de codegen (`codegen-report.mjs`) del hot path de tareas/handles: sin libcalls ni 68020.
- Demos como consumidor real (`build -> run -> analyze`).

Referencias: `DISPLAY_COMPOSITION.md` (buffers y copper), `GRAPHICS_DRIVERS.md` (estado
actual y criterio), `3D_RENDER_VS_PHYSICS.md` (frontera render), `CODING_STYLE.md` (§`inline`).

## 10. Callables del ciclo de vida (plano de comportamiento)

La tarea del comportamiento es **`eng::util::FunctionRef<void()>`** (alias `scene::Task`), la
referencia **no propietaria** del engine (análogo de `std::function_ref`), no `std::function`:
el runtime es freestanding (sin `libstdc++`, sin heap, sin excepciones) y `std::function`
reservaría y arrastraría `<functional>`. `FunctionRef` son **dos punteros** y acepta lambdas
(functors) directamente.

- **Vida**: no posee el callable → debe vivir más que la escena (buffer del llamador). No
  construir la tarea desde un temporal; usar un lambda **con nombre**.
- **Secuencias que esperan ticks** (setup/teardown largos): reutilizar
  `eng::util::TaskSequence<N>` + `TaskStatus` (`eng/core/util/task.hpp`), no inventar otro
  patrón.

### Coste medido (68000, `-O2`, `out/tmp/task-probe.cpp`)

| Caso | Instrucciones | Llamadas |
|---|---:|---:|
| Tarea vía `FunctionRef` en punto **opaco** (parámetro) | 10 | 1 (`jsr (aN)` indirecta) |
| Mismo cuerpo por **plantilla** (functor inline) | 5 | 0 |

Una tarea por invocación cuesta **~1 llamada indirecta (~20–25 ciclos)** y pierde el inlining
del cuerpo. Para el ciclo de vida **por frame** (unas pocas tareas) es **despreciable** frente
a los ~142 000 ciclos de un campo, así que `FunctionRef` se mantiene: da un `Scene` **no
plantilla** y uniforme. Si un hook fuera **por scanline o por elemento**, ahí sí conviene un
hook por **plantilla** (inline, sin `jsr`), a cambio de templatizar el programa (bloat).

Nota: a `-O2`, un lambda **local conocido** se inlinea incluso a través de `FunctionRef`; el
`jsr` aparece solo cuando el callable llega **opaco** (guardado y llamado desde otro punto),
que es el caso real de una tarea almacenada.

### ¿Hace falta poseer el callable? (`InlineFunction<Sig,N>`)

Evaluado: **no ahora**. El llamador ya es dueño del lambda; `FunctionRef` basta. Si algún día
se necesitara guardar un cierre **por valor** con tipo uniforme sin variable con nombre
(p. ej. tareas creadas al vuelo), la pieza sería una `eng::util::InlineFunction<Sig,N>` con
**SBO de capacidad fija** (sin heap) y un `static_assert` del tamaño del cierre; no existe
todavía y se añadiría **solo con consumidor real**. Límite a documentar entonces: `N` debe
cubrir el cierre (unas decenas de bytes; 3–4 punteros suele bastar).

