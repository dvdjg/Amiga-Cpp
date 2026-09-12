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

## Estado y siguientes pasos

- ✅ Cola cooperativa con progreso/rendimiento/adaptación e indicadores bidireccionales,
  integrada en el engine (drenada en el hueco de VBlank). Test host HOST-017.
- ✅ **Drenado en las esperas de Blitter**: `MinimalBackend::set_blitter_service(task,user)`
  ejecuta la cola mientras el backend gira en `BBUSY` (`wait_blitter`), compartiendo el
  mismo cupo por frame que el VBlank (`BackgroundQueue::max_slices_per_frame`). El engine
  lo conecta automáticamente si el backend lo soporta.
- ✅ **Demo `081_background_tasks`**: un proceso pesado (barra progresiva) avanza mientras
  el bucle principal pulsa el fondo y traza una línea por Blitter (cuyas esperas drenan el
  fondo); la tarea adapta su carga a `vpos`.
- Pendiente: **driver por IRQ** (timer de CIA o IRQ de blit nivel 3) para que el fondo
  avance **sin** depender del *polling* de VBlank. Diseño en `C2P_BLITTER.md` §5.1.
