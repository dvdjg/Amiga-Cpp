# Roadmap: presentación de juegos 3D (fotogramas intermedios y horizonte Copper)

Plan de la **capa de presentación** para juegos poligonales estilo *Superhot* / simuladores 90s en A500: percibir 50 Hz con render real a 25 Hz (fotogramas intermedios por paneo de hardware) y fondo de cielo/tierra por offset de scanline (DPF 2+3), sin rellenarlo cada frame. El render 3D (cámara, clipping, mallas) y la física tienen sus propios paquetes en [`3D_GAME_ARCHITECTURE.md`](../../engine/architecture/3D_GAME_ARCHITECTURE.md) (perfil *Superhot Lite*); este roadmap añade lo que no cubren. Diseño de las dos técnicas: [`camera-pan-fake-frames.md`](../../reference/amiga/techniques/camera-pan-fake-frames.md) y [`copper-horizon-dpf.md`](../../reference/amiga/techniques/copper-horizon-dpf.md).

## Estado de partida (piezas que ya existen: no reimplementar)

| Pieza | Dónde | Estado |
|---|---|---|
| Relleno por plano (contorno XOR `ONEDOT` + area fill) | `eng/graphics/polygon_planes.hpp`, `AmigaBackend::fill_polygons_by_plane` | **Hecho**: verificado por la demo 203 y el self-test de 077 ([`ROADMAP_UNIFICADO.md`](ROADMAP_UNIFICADO.md) §«Relleno de polígonos compuesto por bitplane») |
| Línea EOR preparada (octante + error) | `AmigaBackend::blitter_line_eor_prepare/draw` (`backend.hpp:549-560`) | Hecho (077/116/203) |
| Malla, rotaciones, proyección, culling/orden | `core/data/mesh3d.hpp`, `graphics/mesh_renderer.hpp`, `platform/amiga/gfx3d.hpp` | Hecho (077/078/079/116; HOST-011/013/014/046) |
| Near clipping y viewport en el renderer | — | **Falta** (paquete 1 de `3D_GAME_ARCHITECTURE.md`) |
| DPF 2+3 y 3+3 con fino/coarse independiente por playfield | `graphics/drivers/tile_scroll.hpp` | Hecho (110/111/112/202) |
| Emitir una vez + parchear `MoveSlot`; doble buffer de copperlist | `TileScrollScene` (`tile_scroll.hpp:748-810`), `copper::{ListBuilder,DoubleBuffer,Plan}` | Hecho (125/201/202 y el patrón de `tile_scroll`) |
| Ventana de cámara sobre bitmap mayor que el display | `field/xlimited.hpp` (`AxisMode::Finite`, `set_camera`) | X hecho (110/111); **Y finito pendiente** |
| Overlay dinámico sin tocar el playfield | sprites (`graphics/sprite_manager.hpp`), BOBs | Hecho ([`ROADMAP_JUEGO_SPRITES_BOBS.md`](ROADMAP_JUEGO_SPRITES_BOBS.md)) |
| Driver por-scanline de punteros de bitplane | — | **Falta** (misma carencia que documenta [`copper-road-rasters.md`](../../reference/amiga/techniques/copper-road-rasters.md) §«En el engine») |
| Subpíxel de aristas | [`blitter-line-subpixel-fill.md`](../../reference/amiga/techniques/blitter-line-subpixel-fill.md) §2 | Documentado, no implementado; **no prioritario** (fase condicional) |

## Notas de adaptación (decisiones tomadas)

La propuesta de partida (consulta externa, 2026-10) planteaba un árbol `eng::gfx3d` nuevo (`Camera`, `Scene3D`, `FakeFrameController`), un `CustomRegs` crudo y un backend Blitter de `polygon_planes` por hacer. Reconciliada con el estado real:

- **Sin árbol nuevo ni registros crudos**: cámara/rotaciones/proyección se reutilizan (`math3d`, `mesh3d`, `mesh_renderer`) y el chipset se toca solo por `copper::{ListBuilder,Scheduler}` (regla §1.10 de `AGENTS.md`).
- **El backend Blitter de relleno por plano ya está hecho y verificado**: lo que falta es la capa de presentación y el driver por-línea.
- Ángulos/trig: `eng::math::Angle` + tablas del repo (`retro/fixed_trig.hpp`), no tablas propias.
- Memoria: bloques tipados (`Block<Tag, MemoryKind::Chip>`), no `allocate_chip` crudo.
- La presentación se apoya en lo que ya compone el frame: `Band`/`RasterLayout` (`scene/display.hpp`) + `copper::Plan` + drivers de scroll.

## Fases

| Fase | Objetivo | Entregable | Verificación |
|---|---|---|---|
| **F0** | Dependencia: render 3D robusto (cámara, near clipping, buffers acotados) | Paquetes 1–2 de `3D_GAME_ARCHITECTURE.md` (no se duplican aquí) | Los gates de esos paquetes |
| **F1** | Ventana de paneo y contrato Δcámara→píxeles | Test host del mapeo y de la aritmética de márgenes; demo `3d/080` (habitación estática): Full a 25 Hz + paneo a 50 Hz, **sin render nuevo en los Fakes** | Contrato de píxel (fake == framebuffer anterior desplazado), cadencia 50 Hz de display, flicker 0, visión de secuencia (Ollama) |
| **F2** | `PresentController` + pasada completa rebanada por VBlank | Política Full/Fake (umbrales, márgenes, racha, `commit_full`), estados reanudables de la pasada, overlay de balas por sprites | 50 Hz sostenidos con CPU holgada; peor caso (spawn/wrap/cambio de fase) perfilado; `VALIDATION.md` |
| **F3** | Paneo vertical (pitch) | Ventana Y sobre el framebuffer grande (extender `AxisMode::Finite` de XYLimited a Y o ventana dedicada); cuantización a línea | Contrato de píxel del eje Y; sin salto al llegar a margen (Full forzado) |
| **F4** | Horizonte Copper en DPF 2+3 | Driver por-scanline genérico (`WAIT` + par `BPLxPT` por plano; tabla A/B + `scroll_x(y)`) **reutilizable por la carretera** de `ROADMAP_RASTER_ROAD`; demo `3d/081` solo-horizonte (sin 3D) → después con cámara | Matemática de la tabla en host; `eng::setup::check_copper_list` en verde; visión (horizonte estable, sin dientes de sierra); presupuesto Copper medido |
| **F5** | Integración *Superhot Lite* | Demo `3d/082`: habitación + cámara (paneo) + balas (sprites) + horizonte + tiempo lento | 25 fps de render / 50 Hz de display, flicker 0, `VALIDATION.md` con prompts de visión y medidas |
| **F6** | Condicional: subpíxel de aristas | Solo proyección en fixed + preajuste de `derr` en `blitter_line_eor_prepare`; **nunca** durante Fakes | Golden de aristas y A/B medido; sin él, la fase no arranca |

## Contrato de clear (relleno por plano)

Con la ruta `fill_polygons_by_plane` (contorno XOR + area fill) el destino debe partir de un **estado conocido**; el area fill propaga la basura previa y las aristas XOR asumen interior a 0:

1. **Limpiar antes de dibujar** el área de trabajo de PF1 (viewport completo o unión de bboxes de la geometría y los dinámicos); nunca reutilizar «planos sucios».
2. El painter **no borra** siluetas viejas (no es z-buffer): sin clear, el frame anterior se ve detrás de la geometría.
3. El clear por Blitter es barato (un `ClearRect` por plano); empezar por viewport completo y **medir** antes de optimizar a bbox.
4. Capa estática grande: buffer pre-renderizado + copia (memoria Chip extra) o dejarla en PF2; el horizonte ya es esto.
5. Con el cielo/tierra en PF2, el clear de PF1 no limpia «fondo de escena»: solo geometría.

## Presupuesto y estabilidad

- Render real de la escena poligonal: **≥ 25 fps** (suelo de las demos de relleno poligonal, `AGENTS.md` §3); display: **50 Hz** con Fakes.
- Un Fake no puede introducir picos (coste acotado: unos pocos `patch_data` + sprites); un Full no puede saltarse más de un VBlank extra sin convertir el patrón en frame doblado. Perfilar cada fase con `.amigaprofile` antes de darla por cerrada.
- Chip RAM a vigilar: dos framebuffers sobredimensionados × 3 planos, dos bloques de copperlist (8 KB/bloque por defecto), las dos líneas del horizonte (320 B) y los datos de sprites.

## Riesgos y falsación

- **Paralaje del paneo**: con objetos cercanos grandes, el error de un pan uniforme puede notarse con Δ≥1 px/fake. Criterio: si a 2 px/fake la secuencia no convence a la visión, limitar a rotaciones lentas (perfil *Superhot*) o descartar para escenas con traslación.
- **Rebanado**: si la pasada completa no se puede cortar en frontera de VBlank, el patrón no sostiene 50 Hz; el fallback honesto es 25 Hz declarados (nunca telemetría maquillada).
- **Presupuesto Copper**: si la plantilla del horizonte no cabe o compite de más con el fetch de 5 planos, bajar a emisión/parcheo cada 2 líneas (`copper-timing-and-budget.md`).

## Demos y numeración

- Ámbito `demos/techniques/amiga/3d` (077/078/079/084 en uso; consultar `node tools/check/next-number.mjs demos/techniques/amiga/3d`): candidatas `080_camera_pan_window` (F1), `081_horizon_dpf` (F4), `082_superhot_room` (F5).
- Tests host del mapeo de ventana y de la tabla de horizonte: reservar IDs en [`TAXONOMY.md`](../../testing/TAXONOMY.md) al crearlos.

## Referencias

- Técnicas: [`camera-pan-fake-frames.md`](../../reference/amiga/techniques/camera-pan-fake-frames.md), [`copper-horizon-dpf.md`](../../reference/amiga/techniques/copper-horizon-dpf.md), [`copper-road-rasters.md`](../../reference/amiga/techniques/copper-road-rasters.md), [`blitter-line-subpixel-fill.md`](../../reference/amiga/techniques/blitter-line-subpixel-fill.md).
- Arquitectura y estado 3D: [`3D_GAME_ARCHITECTURE.md`](../../engine/architecture/3D_GAME_ARCHITECTURE.md), [`3D_RENDER_VS_PHYSICS.md`](../../engine/architecture/3D_RENDER_VS_PHYSICS.md), [`MODEL3D_ASSET_FORMAT.md`](../../engine/architecture/MODEL3D_ASSET_FORMAT.md).
- Estado vigente: [`ROADMAP_UNIFICADO.md`](ROADMAP_UNIFICADO.md) (§subsistema poligonal y §rasterizado), [`ROADMAP_RASTER_ROAD.md`](ROADMAP_RASTER_ROAD.md), [`ROADMAP_JUEGO_SPRITES_BOBS.md`](ROADMAP_JUEGO_SPRITES_BOBS.md).
