# Métricas de recursos y trazas

El engine publica un bloque de diagnóstico estable, `g_eng_metrics`, con **contadores por frame** y un **anillo de eventos**. Es la vía común para que demos, juegos y módulos internos registren trabajo de Blitter, grupos/celdas Tiled, objetos, actores y cualquier recurso que interese perfilar. No sustituye al perfilador de ciclos (`g_eng_prof`): responde a **qué hizo** el frame, no solo a cuánto tardó.

## Contrato de compilación

La API vive en `engine/include/eng/debug/metrics.hpp`. La build normal **no** define `ENG_METRICS`; `eng::debug::metrics_enabled` vale `false`, el bloque no existe y `MetricsWriter<false>` compila todos sus cuerpos a vacío mediante `if constexpr`. En release no hay stores, ni contadores, ni memoria reservada. Para diagnosticar:

```bash
EXTRA_DEFINES="-DENG_METRICS=1" bash tools/build/build-demo.sh <demo> --release --clean
```

El flag es el único punto de preprocesador de la API; la selección de la ruta instrumentada es C++ (`if constexpr`). Un test puede instanciar `MetricsWriter<false>` para comprobar que no toca memoria.

## Uso

```cpp
eng::debug::metrics.begin_frame(context.frame.frame_index);
eng::debug::metrics.set(eng::debug::Metric::BlitJobs, budget.jobs);
eng::debug::metrics.set(eng::debug::Metric::TiledCellsPatched, patch_stats.cells_patched);
if (patch_stats.groups_deferred != 0u) {
    eng::debug::metrics.event(eng::debug::Metric::TiledGroupsDeferred, patch_stats.groups_deferred);
}
```

`begin_frame` pone a cero los contadores del frame y conserva la traza. `set` escribe un valor absoluto, `add` acumula y `event` añade una entrada al anillo. `event_total` cuenta todos los eventos desde el arranque; el anillo conserva los últimos 16 y sobreescribe el más antiguo. El escritor no es thread-safe: un mismo contador no debe actualizarse desde CPU e IRQ a la vez.

## Catálogo

| Índice | Contador | Significado |
|---:|---|---|
| 0 | `blit_jobs` | jobs de Blitter del frame |
| 1 | `blit_words` | palabras de Blitter del frame |
| 2 | `blit_masked_jobs` | jobs enmascarados |
| 3 | `blit_copy_jobs` | jobs de copia |
| 4 | `copper_words` | palabras de Copper emitidas |
| 5 | `fillup_extra` | blits extra del ajuste de fillup |
| 6 | `tiled_groups_scanned` | grupos Tiled recorridos por el parche |
| 7 | `tiled_groups_patched` | grupos Tiled repintados |
| 8 | `tiled_groups_deferred` | grupos que no cupieron en el plan |
| 9 | `tiled_cells_patched` | celdas Tiled repintadas |
| 10 | `tiled_cells_culled` | celdas descartadas por visibilidad |
| 11 | `tiled_base_restores` | celdas cuyo fondo real se restauró con save-under (no se repinta el bake) |
| 12 | `object_draws` | props/objetos dibujados |
| 13 | `actor_draws` | actores dibujados |
| 14 | `actor_restores` | restauraciones de save-under |
| 15 | `guard_overlays` | repintados estáticos de oclusión |

`eng::playfield::TilePatchStats` ofrece los mismos datos sin acoplarse al bloque de diagnóstico: `patch_animations` tiene una sobrecarga sin estadísticas (coste cero, usada por release) y otra con `TilePatchStats*` (usada por la build instrumentada).

## Lectura desde host

`run-demo.sh --telemetry-samples N` lee por el canal lateral tanto `g_eng_frame_telemetry` como `g_eng_metrics` y guarda las muestras en `run-report.json`:

```bash
bash tools/run/run-demo.sh <demo> --config A500_eng_metrics1_release \
  --telemetry-samples 8 --telemetry-interval-ms 100
node tools/debug/metrics-report.mjs <demo> A500_eng_metrics1_release
```

El informe muestra el último frame, los 16 contadores y los eventos del anillo. Para postmortem, el bloque completo se puede volcar por GDB o por el canal lateral aunque la demo se haya detenido.

## Layout del bloque

`MetricsBlock` es un contrato fijo, big-endian en 68000:

```text
offset 0   u32 magic          "MTRC"
offset 4   u16 version
offset 6   u8  slot_count
offset 7   u8  event_capacity
offset 8   u32 frame
offset 12  u32 values[slot_count]
offset 76  u32 event_total
offset 80  u8  event_head
offset 81  u8  event_count
offset 82  u16 reserved
offset 84  MetricEvent events[event_capacity]   (12 bytes cada uno)
```

Un `MetricEvent` guarda `frame`, `metric`, un `pad` a cero y `value`. El layout no incluye punteros ni tipos del backend: el host puede decodificarlo con una tabla de offsets.

## Relación con Tiled y el trabajo redundante

`TilePatchStats` separa **barrido** (`groups_scanned`), **trabajo útil** (`groups_patched`, `cells_patched`), **trabajo descartado** (`cells_culled`) y **trabajo pendiente** (`groups_deferred`). Un demo puede registrar esos valores sin abrir el plan de cocina ni duplicar su lógica. La telemetría permite decidir si conviene agrupar más celdas, cambiar la paridad o reducir el límite de jobs antes de recurrir a una optimización especulativa.

## Alcance y límites

El bloque mide lo que el código decide registrar; no descubre trabajo por sí solo ni sustituye una captura de pantalla, un frame-diff o la validación con visión. `copper_words` y `fillup_extra` son huecos del contrato que el emisor debe rellenar cuando corresponda; un cero no demuestra que el Copper no emitió palabras. La build instrumentada cuesta stores y unos cientos de bytes de `.data`; sus cifras de fps no son comparables con la build normal.
