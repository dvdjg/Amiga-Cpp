# Referencia — `display` (`eng/api/display.hpp`)

`eng::GameDisplay` es la **descripción declarativa del display** que el juego entrega con
`app.set_display(...)` y que `App::start()` compone y posee. El juego describe geometría, paleta,
efectos de Copper y presupuesto de bus **sin nombrar registros**.

## `GameDisplay`

| Campo | Tipo | Significado |
|---|---|---|
| `width`, `height` | `u16` | tamaño del playfield en px (def. 320×256). |
| `color_depth` | `u8` | número de **planos** (def. 4; 1..`kMaxColorDepth`). |
| `buffers` | `u8` | buffers de display (1 simple; 2/3 doble/triple). |
| `layout` | `PlaneLayout` | `Contiguous` (def.) o `Interleaved`. |
| `palette` | `Palette32` | paleta por defecto. |
| `intents` | `Span<const graphics::CopperIntent>` | **efectos de Copper por línea** (gradientes, zonas de paleta). |
| `bus` | `hw::BusBudgetInput` | **presupuesto de bus** declarado (si no, se deriva del display). |

Constantes: `kDefaultWidth`/`kDefaultHeight`/`kDefaultColorDepth`/`kPaletteEntries`/`kWorldLayerCapacity`.

## `StartError` (motivo de fallo de `App::start`)

`AlreadyStarted` · `MemoryUnavailable` · `InvalidDisplay` · `OutOfMemory` · `CompositionFailed` ·
`BusOverBudget` (la escena no cabe en el bus del A500).

## Helpers

| Helper | Firma | Devuelve |
|---|---|---|
| `scene_resources` | `SceneResources scene_resources(const GameDisplay&)` | recursos de escena (geometría × planos, buffers, layout). |
| `bus_budget_input` | `BusBudgetInput bus_budget_input(const GameDisplay&)` | entrada del presupuesto (la declarada, o una franja derivada). |

```cpp
eng::GameDisplay display {};
display.width = 320; display.height = 256; display.color_depth = 5; display.buffers = 2;
app.set_display(display);
if (!app.start()) { /* StartError */ }
```

> Fuente: `engine/include/eng/api/display.hpp`. Contratos: `docs/engine/architecture/{DISPLAY_COMPOSITION,BUS_BUDGET}.md`.

Volver a [`api/`](README.md) · [Referencia](../README.md) · [índice del manual](../../README.md).
