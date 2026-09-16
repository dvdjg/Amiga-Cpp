# Normalización del repositorio (roadmap)

Plan por fases para dejar **un único dueño por responsabilidad** tras la mezcla de ramas,
eliminar desarrollos duplicados y marcar lo que no está verificado. El modelo objetivo de
buffers y copper está en `docs/engine/architecture/DISPLAY_COMPOSITION.md`.

## Principios de trabajo

- **Por partes**: una fase = un commit (o unos pocos) con su **gate** propio; si el gate
  falla, se revierte la fase, no se acumula deuda.
- **Gate por fase**: suite host + regresión de las demos afectadas + gate visual/secuencia
  cuando toque render.
- **Nada sin consumidor**: una API del engine sin demo ni test se marca **NO VERIFICADA**
  y no se documenta como validada.
- **No duplicar**: si algo existe, se generaliza o se sustituye; nunca se añade una variante
  paralela.

## Fases

### F0 — Higiene y verdad documental (sin cambio funcional)

| # | Tarea | Fichero | Estado |
|---|---|---|---|
| 0.1 | Arreglar la rama no-ASM de la 080 (`m_scene[active]` sin declarar) | `demos/amiga/080_fire_rgb/src/main.cpp:361` | **hecho** (`418dc52`) |
| 0.2 | Corregir el comentario: `XlimitedDisplayComposer` **reemite** la lista completa, no parchea 13 words | `engine/include/eng/field/xlimited.hpp:1488-1491` | **hecho** |
| 0.3 | Marcar `DoubleBufferedHiddenMargins` / `TileScrollStrategy::double_buffered` como **política no implementada** | `engine/include/eng/scene/virtual_scene.hpp:168,186` | **hecho** |
| 0.4 | Test host de `DoubleBufferScrollPlayfield` (2 bitmaps, `flip()`, `hardware_view()` del delantero) | `tests/host/068_double_buffer_scroll` | **hecho** |
| 0.5 | Test host de `TileScrollScene` (geometría de la lista + alternancia de los 2 bloques + 13 words parcheadas) | `tests/host/069_copper_double_buffer` | **hecho** |
| 0.6 | Crear los documentos canónicos de F0: contrato (`DISPLAY_COMPOSITION.md`) y roadmap (este) + enlazarlos en los índices | `docs/engine/architecture/`, `docs/guides/roadmap/` | **hecho** |
| 0.7 | Re-medir y corregir las cifras de fps de `AGENTS.md` (101/102/103/104) o indicar su contexto de medida; hoy 103 mide 32,7 y no 50 | `AGENTS.md` | **investigado, pendiente de bisect** (ver abajo) |

Gate: suite host + encoding. Sin cambios de comportamiento, así que no exige regresión de demos.

### F1 — `CopperDoubleBuffer` (una sola implementación del doble buffer de copperlist)

| # | Tarea | Fichero | Estado |
|---|---|---|---|
| 1.1 | Extraer `eng::copper::DoubleBuffer` (2 bloques + `flip` + `install`/`takeover` + `inactive_scheduler`), con soporte de **handles** de parcheo en `Scheduler` (`move_at`/`patch_data`) | `engine/include/eng/graphics/copper/double_buffer.hpp`, `copper/scheduler.hpp` | **hecho** |
| 1.2 | Usarlo en `TileScrollScene`, sustituyendo los offsets cableados (`words[5]`, `words[21+4p]`) por handles | `engine/include/eng/graphics/drivers/tile_scroll.hpp` | **hecho** |
| 1.3 | Usarlo en `XlimitedDisplayComposer` y `XlimitedDualComposer` (y decidir `Patch` vs `Reemit` con dato de coste) | `engine/include/eng/field/xlimited.hpp:1739,1928` | pendiente |
| 1.4 | Documentar la receta «2 bloques + install tras VBlank, nunca COPJMP1» en `C2P_BLITTER.md`/`GRAPHICS_DRIVERS.md` | `docs/engine/architecture/` | pendiente |

Gate F1.2: demos `101/103/104/105` idénticas (analyze + fps). Gate F1.3: demos
`107/110/111/112/120/121/201/202`.

### F2 — `MultiBuffered` como única capa de buffers de display

| # | Tarea | Fichero | Estado |
|---|---|---|---|
| 2.1 | Migrar `082_plasma` y `083_fbm_noise` (hoy 2 instancias + `m_active` manual) | `demos/amiga/082_plasma`, `083_fbm_noise` | pendiente |
| 2.2 | Exponer **N por configuración** (`-DK_<DEMO>_BUFFERS=1\|2\|3`) y documentar «con y sin doble/triple buffer» | `demos/amiga/061…`, `080…` | pendiente |
| 2.3 | Evaluar `079` (anillo de 5, rastro temporal) y `116` (triple buffer real): ¿encajan como `MultiBuffered<Driver,N>` o son otro caso? | `demos/amiga/079…`, `116…` | pendiente |

Gate: regresión de esas demos (fps + gate visual).

### F3 — Superficies/capas sin memoria de display

| # | Tarea | Fichero | Estado |
|---|---|---|---|
| 3.1 | `DoubleBufferScrollPlayfield` pasa a **superficie ligada a slots** (conserva cámaras/mapper/`hardware_view`; deja de poseer `Bitmap[2]`) | `engine/include/eng/field/double_buffer_playfield.hpp:100` | pendiente |
| 3.2 | Migrar la demo `122` al nuevo contrato | `demos/amiga/122_doublebuffer_scroll` | pendiente |
| 3.3 | Decidir el destino de `FlatScrollPlayfield`/`MirrorScrollPlayfield` (misma regla: no poseer memoria) | `engine/include/eng/field/{flat,mirror}_playfield.hpp` | pendiente |
| 3.4 | `PlaneView`/`SoftDpfComposition`: documentar que es flip **de un plano** (excepción deliberada) | `plane_view.hpp:43-65`, `soft_dpf.hpp` | pendiente |

Gate: demo `122` + gate visual/secuencia; host `038/039`.

### F4 — `CopperPlan`: orquestación de copper a nivel de escena

| # | Tarea | Fichero | Estado |
|---|---|---|---|
| 4.1 | Definir `CopperPlan` (un plan por buffer de display: recolecta `CopperIntent`/`SpriteIntent`, ordena por scanline, `Patch`/`Reemit`, handles, presupuesto) | `engine/include/eng/graphics/copper/plan.hpp` | pendiente |
| 4.2 | Integrarlo con `MultiBuffered` (el plan se rellena en el buffer trasero y se publica en `commit`) | `engine/include/eng/graphics/drivers/multi_buffered.hpp` | pendiente |
| 4.3 | Portar como **tracks** los casos que hoy emiten copper a mano: sky, splits, paleta por zona, scroll, HUD, sprites; empezar por `055_copper_rainbow` y una zona de paleta del XLimited | demos `055`, campo XLimited | pendiente |
| 4.4 | Test host del `CopperPlan` (orden por línea, patch vs reemisión, handles válidos, presupuesto) | `tests/host/<n>_copper_plan` | pendiente |

Gate: demos de copper idénticas + test host + informe de presupuesto sin spill.

### F5 — Unificar los dos mundos de scroll

| # | Tarea | Fichero | Estado |
|---|---|---|---|
| 5.1 | Unificar el mapper de scroll (`map_ring_scroll` vs `TileScrollScene::compute_display`) | `amiga_display_mapper.hpp:52`, `tile_scroll.hpp:670` | pendiente |
| 5.2 | Decidir el destino de `TileScrollScene` y de las demos `100/101/103/104/105` (¿canónica, laboratorio o retirada?) | `SCROLL_DEMOS_CLEANUP.md` | pendiente |
| 5.3 | Unificar cámaras (`Camera2D`, `RouteCamera`, `BigBufferScroll`, `CameraQ16`) en un solo vocabulario | `scene/`, `field/` | pendiente |
| 5.4 | Decidir `XlimitedScene` vs `VirtualScene` (y si nace `DisplayComposition`) | `field/xlimited_scene.hpp`, `scene/virtual_scene.hpp` | pendiente |

Gate: regresión de las demos de scroll + docs actualizados.

## Incongruencias detectadas (checklist)

| Incongruencia | Dónde | Fase |
|---|---|---|
| Doble buffer de copperlist reimplementado 4 veces | `tile_scroll.hpp:791`, `xlimited.hpp:1739,1928`, `079`, `116` | F1 |
| Doble buffer de display a mano (2/3/5 buffers) | `082`, `083`, `079`, `116` | F2 |
| Superficie que posee bitmaps y hace flip (duplica el display) | `double_buffer_playfield.hpp:100` | F3 |
| Mapper de scroll duplicado | `amiga_display_mapper.hpp:52` vs `tile_scroll.hpp:670` | F5 |
| Tres motores de scroll/tiles | `field/scroll_engine.hpp`, `drivers/tile_scroll.hpp`, `tilemap/tile_scroll.hpp` (+ `field_controller.hpp` legacy) | F5 |
| Cámaras duplicadas (4+) | `scene/`, `field/`, demos | F5 |
| «Escena» duplicada | `scene::VirtualScene` vs `field::XlimitedScene` | F5 |
| Política de buffers sin implementación | `virtual_scene.hpp:168,186` | F0 |
| Doc↔código (13 words vs reemisión) | `xlimited.hpp:1488-1491` | F0 |
| Referencia con deriva de línea | `DEBUG_DEMO_ARRANQUE_DOBLE_TEXTO_BANDA.md:189` | F0 |
| Cifras de fps de `AGENTS.md` desactualizadas: dice 103≈50 fps y hoy mide 32,7 (A/B con y sin `DoubleBuffer`: 32,67 vs 33,50 → **no** es del refactor, es del merge) | `AGENTS.md` (nota de rendimiento del scroll) | F0 |
| Supervisión de copper por escena inexistente | — (nace en F4) | F4 |
| NO VERIFICADAS sin consumidor | `PolygonFillSink` (`platform/amiga/polygon_fill.hpp:14`, `field/playfield.hpp:87`), `CameraQ16` (`field/tile_demo.hpp:16`) | F0/F5 |
| Tests host que solapan demos (o al revés) | `038/039` vs `112`; `067` vs `061/080`; `044` vs `120`; `061` vs `120/121/122` | transversal |

### Nota sobre las cifras de fps (0.7)

Evidencia recogida (medida con `measure-fps.mjs`, A500 `-O1`):

| Demo | `AGENTS.md` | medido hoy | A/B sin `DoubleBuffer` |
|---|---|---|---|
| 101 | ~48 | **49,92** | — |
| 103 | ~50 | **33,50** | 32,67 |
| 104 | ~47,6 | **30,12** | — |

- **No es de los refactors de normalización**: el A/B de la 103 (con y sin `MultiBuffered`/`DoubleBuffer`) da 33,50 vs 32,67.
- **101 sí cuadra** con la nota; 103/104 no. Los READMEs de esas demos no citan fps.
- **No se puede comparar contra un commit antiguo** de forma directa: al hacer checkout cambian también el layout de `out/demos/...` y las propias herramientas (los números de la nota pueden ser de ese layout anterior), así que el A/B envejecido no es fiable.
- Receta para cerrarlo: `git bisect` entre el commit donde se escribió la nota de `AGENTS.md` y `HEAD`, midiendo 103 en cada paso, **con el tooling de cada commit** (o, mejor, fijar el artefacto: medir siempre con el runner actual y anotar fecha+config junto a cada cifra). Mientras no se haga, las cifras de fps de `AGENTS.md` deben considerarse **no trazables**, no un objetivo.

## Decisiones pendientes (requieren consulta)
1. ¿`TileScrollScene` se promueve a canónico o queda como laboratorio y se retiran sus demos?
2. ¿Se renombra `XlimitedScene` → `DisplayComposition` (como propone `PLAYFIELD_SCROLL_ARCHITECTURE.md`)?
3. ¿`TileFieldController` (4 páginas) se retira?
4. Para el doble buffer del anillo XLimited: ¿se duplica el anillo (+80 KB y 2× blitter en 201) o se amplía solo el plano de fondo (soft DPF, ya probado)?
