# Referencia — `hud/`

El **HUD de juego** del engine (`eng::hud`): modelo portable de componentes (medidores, contadores, rejillas de iconos, slots, arrastre), contrato de **región** con presupuesto y un **presentador de franja** sobre `CanvasPlayfield`/`Surface`. El HUD **no lee ni escribe hardware**: declara la región y el compositor de escena (`RasterLayout`/`ModeSwitchZone`) publica la banda. Ver `docs/engine/architecture/HUD_SYSTEM.md`.

```
   juego (fuente de verdad) ──set_*()/vistas──► Hud<...>  (modelo, handles, dirty)
                                                    │ HudDrawOp / HudHitRect
                                                    ▼
   HudRegionSpec (Band|Overlay|Screen) ──► HudStrip.apply() ──► Surface de la banda
                                                    │
   RasterLayout/ModeSwitchZone ◄── band_of_planes ──┘  (el App materializa la copperlist)
```

## Páginas

| Cabecera | Qué documenta |
|---|---|
| `model.hpp` | `HudHandle<Tag>` y handles (`MeterHandle`, `CounterHandle`, `GridHandle`, `LabelHandle`, `ButtonHandle`, `PanelHandle`, `RegionHandle`), `Meter`/`Counter` (formato sin división: potencias por sumas repetidas + sustracción), `IconGrid`/`HudSlotView` (flags `kHudSlot*`), `Label`/`Button`/`Panel`, `DragSession` (`DragPhase`, `kHudDragStartPx`), `HudDrawOp`/`HudHitRect`/`HudGridHit` y `Hud<MaxRegions, MaxWidgets, MaxSlots, MaxDrawOps, MaxHitRects>` (alta con capacidad fija, `set_*` con *dirty* solo al cambiar el valor visible, `bind_grid`, `grid_select`/`grid_move_focus`, transiciones de `drag_*`, `push_op`/`push_hit`, `dirty_revision`/`any_dirty`/`clear_dirty`). |
| `region.hpp` | `HudPlacement` (`Band`/`Overlay`/`Screen`), `HudOverlayRestore` (`GameRedraws`/`Backing`), `HudBand` (`top` de **raster**, `height`, `planes`, `own_palette`), `HudBudget` (ops/blits/moves Copper/backing/canales de sprite/BOBs), `HudRegionSpec` (`z`, `capture_input`, tema como `Ref`), `kHudDisplayLines`/`kHudMaxPlanes` y validadores puros `band_valid`/`region_valid`. |
| `strip.hpp` | `HudStripConfig` (ancho/alto/planos/fondo), `apply_hud_ops` (`Fill`/`Frame`/`Text`/`Digits` con recorte; texto por palabras si la celda está alineada a 16 px) y `HudStrip` (`bind` a bloque Chip con validación de tamaño, `clear`, `apply`, `surface`, `canvas`). |
| `theme.hpp` | `MeterStyle` (`Linear`/`Segmented`/`Pips`/`Raster`), `HudFont`, `IconAtlas` (`ChipView<HudAtlasTag>`, `frame_w/h`, `count`, `stride_words`, `planes`, `masked`) y `HudTheme` (paleta de banda, roles de color y métricas). |
| `hud.hpp` | **Fachada** del módulo (la incluye `eng/api/api.hpp`). |

## Invariantes

- **El juego manda**: el HUD mantiene estado de presentación (valor mostrado, selección, *hover*, arrastre); los valores y los slots llegan por `set_*` o como `HudSlotView` (observador). No posee objetos ni reglas.
- **`Band::top` es línea de raster** (el `WAIT` del Copper compara VPOS): la línea de display 208 empieza en `DIWSTRT_y + 208` (0x2c + 208; AHRM cap. 3). La franja no encoge el anillo de scroll: solo el área visible.
- **El presentador no decide el fondo**: pinta operaciones; el llamador emite los borrados/deltas (la barra que encoge, el pip que cambia o la celda de dígitos). En celdas alineadas a 16 px, `draw_text_words` sustituye la palabra completa (patrón y fondo) y no necesita borrado.
- **Sin heap ni `virtual`**: capacidades de plantilla, `HudHandle` de índice `u8` (≤255), listas de capacidad fija y despacho por `switch`/tipo de operación.
- **La región no toca el chipset**: `HudRegionSpec` es un descriptor; el compositor de escena la materializa (`LayerPlan`/`CanvasPlayfield`/`ModeSwitchZone`) y valida el split con `eng::setup`.

## Verificación

- **HOST-467** (`tests/host/ui/467_hud_model`): modelo, handles, capacidad, *dirty*, contadores (bases 10/16/2, ceros, negativos, `s32` mínimo), rejilla y `DragSession`; dos configuraciones de capacidad.
- **HOST-468** (`tests/host/ui/468_hud_strip`): `HudStrip` (bind/validación/limpieza) y `apply_hud_ops` (`Fill`/`Frame`/`Text` con recorte; ruta de palabra completa).
- **Demo `302_hud_band`** (`demos/features/ui/amiga/302_hud_band`): franja de 2 planos con paleta propia en la línea 208, HUD por deltas, **49,7 fps / 1,005 campos** (A500_debug), *flicker* declarado y visión A/B/C + canónica (`VALIDATION.md`).
- Pendiente (H2–H6): hit-test/foco, arrastre, overlay/sprites, paneles y atlas (plan en `docs/guides/roadmap/ROADMAP_HUD.md`).

Volver a [Referencia](../README.md) · [índice del manual](../../README.md).
