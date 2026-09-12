# Tareas de fondo (cooperativas)

En un Amiga hay **una sola CPU** (sin hilos ni procesos concurrentes). Cuando una
demo o un juego necesita hacer trabajo pesado que **no** debe retrasar el frame
(preprocesar datos, descodificar assets, precargar tiles, calcular visibilidad,
simular algo opcional…), la única forma es **cooperativa**: repartir ese trabajo en
trozos y ejecutarlos **solo cuando el bucle principal no tiene nada que hacer**.

`eng::task::BackgroundQueue` (`engine/include/eng/task/background.hpp`) implementa
ese bloque. No usa heap (pool fijo), no introduce hilos y **garantiza prioridad al
bucle principal**: `update`/`render` se llaman siempre primero; las tareas de fondo
solo corren en los huecos.

## Modelo

```
                       +-------------------- BackgroundQueue -------------------+
   juego  --add-->     |  TaskStep(data, slice) -> unidades                     |
   juego  --progress-> |  {state, done, total, permille, avg_units_per_slice}  |
   tarea  <--slice--   |  {frame, vpos, budget, done, total, avg}               |
                       +------------------------------------------------------+
```

- Una **tarea** es una rutina `u16 (*)(void* data, const TaskSlice& slice)` que
  procesa un trozo y devuelve las **unidades** consumidas (≤ `slice.budget_units`),
  o `task_abort` para fallar.
- Es **finita** si se registra con `total_units > 0` (termina al alcanzarlo);
  es **continua** con `total_units = 0` (la cancela el juego).
- El **handle** (`TaskHandle {index, generation}`) sobrevive a la reutilización de
  slots: un handle de tarea terminada queda inválido.

## Información bidireccional (barata)

- **Tarea → mundo**: cada rebanada recibe `TaskSlice { frame, vpos, budget_units,
  done_units, total_units, avg_units_per_slice }`. Con `vpos` (línea de raster del
  CRT), `frame` y sus propios indicadores, la tarea **adapta su carga** (p. ej.
  procesar más al principio del frame y menos si ya está por terminar, o autoajustar
  el tamaño de trozo a su `avg` medido).
- **Juego → tarea**: `progress(handle)` devuelve `TaskProgress { state, done_units,
  total_units, permille, avg_units_per_slice, slices }`, con `remaining_units()` y
  `finished()`. El juego sabe cuánto le falta a un proceso finito sin coste.
- La "escena" o cualquier dato del juego se pasan como `void* data` (una struct del
  llamador); la tarea puede leer punteros del mundo en `data` si los necesita.

## Prioridad y presupuesto

El `Engine` posee una `BackgroundQueue` y la publica en `GameContext::background`.
La drena desde el **hueco de VBlank** (la tarea ociosa que el backend ejecuta
mientras sondea `VPOSR`):

- como máximo `max_slices_per_frame` rebanadas por frame (defecto 4), para acotar el
  coste de fondo;
- cada rebanada usa el `slice_units` configurado por tarea en `add()`;
- `update`/`render` se ejecutan siempre antes y después del hueco, así que un fondo
  mal calibrado puede alargar el frame pero **nunca** adelantar trabajo del juego.

Ajuste práctico: subir `slice_units` hace trozos más grandes (menos overhead, más
riesgo de pasarse del hueco); bajarlo da un fondo más "granular". `vpos` permite a
la tarea decidir por sí misma.

## Uso

```cpp
// init(): registrar una rutina finita (p. ej. preparar una tabla para el nivel 2).
m_prepare = context.background->add(&prepare_level_step, &m_prepare_data,
                                   /*total_units*/ kCells, /*slice*/ 128);

// update(): ¿cuánto falta? (p. ej. para una barra o para decidir una transición).
const auto p = context.background->progress(m_prepare);
if (p.finished()) { m_level_ready = true; }

// La rutina ve el raster y su progreso, y adapta:
u16 prepare_level_step(void* data, const eng::task::TaskSlice& s) {
    auto* d = static_cast<PrepareData*>(data);
    const u16 n = (s.vpos > 200u) ? static_cast<u16>(s.budget_units / 2u) : s.budget_units;
    return d->process(n);            // devuelve las unidades realmente hechas
}
```

## Dos modos de bucle

El engine soporta dos organizaciones, segun dónde viva el **juego** y dónde el **fondo**:

| Modo | Juego (`update`/`render`) | Fondo | API |
|---|---|---|---|
| **Interrupt-driven** (por defecto) | **IRQ de VBlank** (latido del juego, *deadline* de 1 frame) | bucle principal (`while (frames < N) background.run_slice(...)`) | `Engine::run_frames` |
| **Polling** (alternativo) | bucle principal (`update -> wait_vblank -> render`) | drenado en el hueco de VBlank y en las esperas de Blitter | `Engine::run_frames_polling` |

El modo **por defecto** es el más natural en Amiga y el que **no quema ciclos en *polling***:
la CPU nunca espera al VBlank, solo trabaja (el juego en la IRQ, el fondo en el bucle). El
tick del juego mide su coste en **líneas de raster** (`GameContext::irq`: `last_lines`,
`max_lines`, `overruns`, `budget_lines`) para vigilar el presupuesto.

En el modo **interrupt-driven** (el más fiel al estilo Amiga clásico) la IRQ de VBlank
lleva el trabajo del juego (avanzar animación, actualizar el Copper, input) con **prioridad
dura**: preempta al fondo. El bucle principal ejecuta el trabajo de fondo cooperativo; cuando
la IRQ no tiene nada más que hacer, vuelve (`RTE`) y el fondo continúa.

Implementación: `support/level3_irq.s` es el **handler único del autovector de nivel 3**
(`0x6C` en 68000 / `VBR+0x6C`), porque `VERTB`, `BLIT` y `COPER` **comparten vector**. Salva
registros, despacha a C++ y hace `RTE`; el despacho lee `INTREQR` y atiende cada fuente
limpiando su bit:

- `set_vblank_service(task, user)` → tick del juego (`Engine::InterruptTick`: `update`+`render`).
- `set_blit_service(task, user)` → servicio de blit (encadenar blits / drenar el fondo).

El fondo del bucle principal usa `BackgroundQueue::run_slice`, con la **guarda de reentrada**
(`in_slice`) por si una IRQ lo preempta a mitad de rebanada.

## ¿VBlank, blit o timer?

| Fuente | Qué da | Uso |
|---|---|---|
| **VBlank IRQ** (`VERTB`, nivel 3) | tick 50 Hz | **latido del juego**: garantiza la cadencia aunque el fondo sea pesado |
| **Blit IRQ** (`BLIT`, nivel 3, mismo vector) | evento "blit terminado" | encadenar blits / drenar el fondo sin *polling* de `BBUSY` |
| **Timer CIA-A** (nivel 2) | **reloj propio** (o reloj de tiempo real TOD) | **motor de fondo** independiente del frame |

El tick del juego mide su coste en **líneas de raster** (`GameContext::irq`), no en ciclos: es
lo que hay en hardware real.

### Timer de CIA-A (implementado)

`MinimalBackend::background_timer_start(latch, task, user)` programa el **timer A continuo**
(reloj E), enmascara su IRQ en la CIA (`ICR`) y monta el handler de **nivel 2** (`0x68`).
Corre a `latch / 709379` s por tic. La demo 081 lo usa con `latch = 0x2000` → **~86 IRQ/s**
(medido).

- ✅ **Reloj de tiempo real**: la misma CIA-A tiene **TOD** por hardware.
  `MinimalBackend::cia_tod_ticks()` lo lee (24 bits, `TODHI→TODMID→TODLO`) y
  `eng::time::from_tod` (`eng/core/rtc.hpp`, test host **HOST-018**) lo convierte a hora del
  día; la demo 081 publica los segundos del RTC (avanzan a 1 Hz, verificado).

> Lección (referencia `amiga-bootcamp/01_hardware/common/cia_chips.md`): en el **CRA**,
> `bit 3 RUNMODE` es **0 = continuo, 1 = one-shot**. Poner one-shot hace que el timer dispare
> **una sola vez** y parezca "no recargar". Ese era el bug del primer intento.

## Estado

- ✅ Cola cooperativa (`BackgroundQueue`): progreso/rendimiento/adaptación e indicadores
  bidireccionales. Test host **HOST-017**.
- ✅ **Drenado en las esperas de Blitter** (`set_blitter_service`, *polling* de `BBUSY`).
- ✅ **Modo interrupt-driven por defecto** (`Engine::run_frames`): la IRQ de VBlank lleva
  `update`/`render` (con telemetría de líneas de raster) y el bucle principal el fondo. El
  modo *polling* queda como `run_frames_polling`.
- ✅ **Handler único de nivel 3** (`support/level3_irq.s`) con **servicio de blit** por IRQ.
- ✅ **Motor de fondo por timer de CIA-A** (`background_timer_start`, ~86 Hz medido).
- ✅ **Demo `081_background_tasks`**: juego en la IRQ de VBlank (pulso + línea por Blitter),
  fondo en el bucle principal + drenado por blit IRQ + timer de CIA.
