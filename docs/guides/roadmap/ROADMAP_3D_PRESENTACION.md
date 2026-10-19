# Roadmap: presentación de juegos 3D (fotogramas intermedios y horizonte Copper)

Plan de la **capa de presentación** para juegos poligonales estilo *Superhot* / simuladores 90s en A500: percibir un display fluido (≥25 Hz) con pasadas completas de render más lentas (suelo **12,5 Hz**), interpolando entre pasadas con paneo de hardware y varios fotogramas falsos por pasada según el coste medido, y fondo de cielo/tierra por offset de scanline (variante *RoboCod* de 5 planos como principal y *DPF 2+3* como alternativa), sin rellenarlo cada frame. El render 3D (cámara, clipping, mallas) y la física tienen sus propios paquetes en [`3D_GAME_ARCHITECTURE.md`](../../engine/architecture/3D_GAME_ARCHITECTURE.md) (perfil *Superhot Lite*); este roadmap añade lo que no cubren. Diseño de las dos técnicas: [`camera-pan-fake-frames.md`](../../reference/amiga/techniques/camera-pan-fake-frames.md) y [`copper-horizon.md`](../../reference/amiga/techniques/copper-horizon.md).

## Estado de partida (piezas que ya existen: no reimplementar)

| Pieza | Dónde | Estado |
|---|---|---|
| Relleno por plano (contorno XOR `ONEDOT` + area fill) | `eng/graphics/polygon_planes.hpp`, `AmigaBackend::fill_polygons_by_plane` | **Hecho**: verificado por la demo 203 y el self-test de 077 ([`ROADMAP_UNIFICADO.md`](ROADMAP_UNIFICADO.md) §«Relleno de polígonos compuesto por bitplane») |
| Línea EOR preparada (octante + error) | `AmigaBackend::blitter_line_eor_prepare/draw` (`backend.hpp:549-560`) | Hecho (077/116/203) |
| Malla, rotaciones, proyección, culling/orden | `core/data/mesh3d.hpp`, `graphics/mesh_renderer.hpp`, `platform/amiga/gfx3d.hpp` | Hecho (077/078/079/116; HOST-011/013/014/046) |
| Primitivas 3D generadas en compilación (caja, prisma, tubo, cono, esfera, cápsula, platónicos) | `eng/retro/mesh_primitives.hpp` (`MeshData<S, MaxV, MaxI, MaxF>`, `fill_*`) | Hecho (HOST-477); volcado a `.cpp`/UAF desde tool host: pendiente |
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

Estado (2026-10-18): **F1 parcialmente ejercitada** — demo `3d/080_camera_pan_window` creada (host tests HOST-470 de la política de paneo y HOST-471 del pacing adaptativo, contrato de píxel del mecanismo probado por aislamiento y en secuencia, telemetría y visión OK). La demo decide **cuántos fakes admite cada pasada** con el coste medido del Full (`eng::graphics/render_pacing.hpp`: EMA en campos PAL, `affordable_fakes`), con el display a ~28 fps (suelo de display ≥25 Hz cumplido); la **base** aún no llega a 12,5 Hz: el Full mide ~3,4 campos y con la regla actual sale K=2 (~9 Hz de base) — cerrar F1 exige dejarlo en ≤3,0 campos (K=1, base ≥12,5 Hz) con la campaña de optimización del Full (siguiente paso verificable; deuda DT-032…DT-034 en `ROADMAP_DEUDA_TECNICA.md`, evidencia y alcance en el `VALIDATION.md` de la demo). Estado (2026-10-18, tarde): el clip 2D del engine quedó corregido (arista de cierre, HOST-479; tramos > s16, DT-041) y la 080 avanza más, pero en ejecución normal se detiene a los ~12 frames por el parcheo de la copperlist activa (DT-032, ampliación con perfil por secciones y ablaciones); las secuencias de contrato solo son válidas en ventanas cortas hasta cerrar DT-032.

| Fase | Objetivo | Entregable | Verificación |
|---|---|---|---|
| **F0** | Dependencia: render 3D robusto (cámara, near clipping, buffers acotados) | Paquetes 1–2 de `3D_GAME_ARCHITECTURE.md` (no se duplican aquí) | Los gates de esos paquetes |
| **F1** | Ventana de paneo y contrato Δcámara→píxeles | Test host del mapeo y de la aritmética de márgenes; demo `3d/080` (habitación estática): pasada completa ≥12,5 Hz y display interpolado ≥25 Hz con K adaptativo (**sin render nuevo en los Fakes**) | Contrato de píxel (fake == framebuffer anterior desplazado), display ≥25 Hz y base ≥12,5 Hz, flicker 0, visión de secuencia (Ollama) |
| **F2** | `PresentController` + pasada completa rebanada por VBlank | Política Full/Fake (umbrales, márgenes, racha adaptativa por coste, `commit_full`) con **publicación diferida al inicio de campo** — **hecha** (`eng::graphics::present_controller.hpp`, HOST-476; integrada en la 080: frames enteros y contrato en verde); **estados reanudables de la pasada** y overlay de balas por sprites — pendientes. El rebanado se intentó (etapas clip → relleno sala → relleno paneles, una por update, con los builders como estado del llamador; el display continúa presentando desplazamientos entre etapas) pero quedó **sin verificar**: la primera implementación renderiza bien los primeros frames y a partir de ~13 el proceso acaba en ROM (reproducido después SIN rebanado: era el parcheo de la copperlist activa, DT-032, no el rebanado), y la ruta de verificación por captura de paso quedó fiable con `--reset-emulator` (DT-033). Próximo intento: instrumentar el paso con la consola de depuración (0xB70000) y capturar el postmortem con el emulador estable antes de integrar; **rebanado implementado (2026-10-19)**: etapas clip → sala (plano 0 y 1) → paneles, una por update, con publicación diferida; verificado pixel-idéntico a HEAD a pan 0/−13/+13 y medido en `VALIDATION.md` §11 (display 32–36 Hz; base ~9 Hz, sigue corta: el rebanado reparte el Full pero no reduce su trabajo). El mismo barrido descubrió un defecto **preexistente** a yaw positivo (DT-043: sala invertida, también en HEAD), ajeno al rebanado; el diseño de etapas está descrito en el commit de la prueba. **Render asíncrono por IRQ de blit**: el engine ya tiene la cola (`AmigaBackend::enable_blitter_queue`, INTF_BLIT → nivel 3, `begin/one/kick/drain`) — su veredicto medido (campaña 218, `218-campana-rendimiento.md` §resumen) es que **solo paga cuando hay CPU que solapar** (268k vs 221k sin trabajo solapable; el encolado en Slow costaba ~6k/job con el Blitter en marcha); en A500 con Fast RAM el CPU corre a plena velocidad mientras el Blitter ocupa el bus y es el caso ideal, así que el `PresentController` debe decidir el camino por hardware (`eng::hw`) | Display ≥25 Hz sostenidos con CPU holgada; peor caso (spawn/wrap/cambio de fase) perfilado; `VALIDATION.md` || **F3** | Paneo vertical (pitch) | Ventana Y sobre el framebuffer grande (extender `AxisMode::Finite` de XYLimited a Y o ventana dedicada); cuantización a línea | Contrato de píxel del eje Y; sin salto al llegar a margen (Full forzado) |
| **F4** | Horizonte Copper por scanline (variante **RoboCod** de 5 planos; DPF 2+3 como alternativa) | Driver por-scanline genérico (`WAIT` + par `BPLxPT` por plano; tabla (orientación, k, S) con las 16 filas variantes de frontera horneadas) **reutilizable por la carretera** de `ROADMAP_RASTER_ROAD`; demo `3d/081` solo-horizonte (sin 3D) → después con cámara | Matemática de la tabla en host; `eng::setup::check_copper_list` en verde; visión (horizonte estable, sin dientes de sierra); presupuesto Copper medido |
| **F5** | Integración *Superhot Lite* | Demo `3d/082`: habitación + cámara (paneo) + balas (sprites) + horizonte + tiempo lento; **culling de entrada por capas** (`3D_GAME_ARCHITECTURE.md` §Visibilidad: esfera/frustum ya en HOST-478; sectores/portales antes de la 082) | Full ≥12,5 Hz / display ≥25 Hz, flicker 0, `VALIDATION.md` con prompts de visión y medidas |
| **F6** | Condicional: subpíxel de aristas | Solo proyección en fixed + preajuste de `derr` en `blitter_line_eor_prepare`; **nunca** durante Fakes | Golden de aristas y A/B medido; sin él, la fase no arranca |

## Contrato de clear (relleno por plano)

Con la ruta `fill_polygons_by_plane` (contorno XOR + area fill) el destino debe partir de un **estado conocido**; el area fill propaga la basura previa y las aristas XOR asumen interior a 0:

1. **Limpiar antes de dibujar** el área de trabajo de PF1 (viewport completo o unión de bboxes de la geometría y los dinámicos); nunca reutilizar «planos sucios».
2. El painter **no borra** siluetas viejas (no es z-buffer): sin clear, el frame anterior se ve detrás de la geometría.
3. El clear por Blitter es barato (un `ClearRect` por plano); empezar por viewport completo y **medir** antes de optimizar a bbox.
4. Capa estática grande: buffer pre-renderizado + copia (memoria Chip extra) o dejarla en el plano de fondo del display (plano 4 del horizonte o PF2 en DPF); el horizonte ya es esto.
5. Con el cielo/tierra en PF2, el clear de PF1 no limpia «fondo de escena»: solo geometría.

## Presupuesto y estabilidad

- Pasada completa (render real de la escena poligonal): **≥12,5 Hz**; display con Fakes: **≥25 Hz**. (El presupuesto de las demos de relleno poligonal de `AGENTS.md` §3 se lee con estos dos suelos: base e interpolado.)
- Un Fake no puede introducir picos (coste acotado: unos pocos `patch_data` + sprites); un Full no puede saltarse más de un VBlank extra sin convertir el patrón en frame doblado. Perfilar cada fase con `.amigaprofile` antes de darla por cerrada.
- Chip RAM a vigilar: dos framebuffers sobredimensionados × 3 planos, dos bloques de copperlist (8 KB/bloque por defecto), las filas del horizonte (1,3–2,7 KB según orientaciones) y los datos de sprites.

## Riesgos y falsación

- **Paralaje del paneo**: con objetos cercanos grandes, el error de un pan uniforme puede notarse con Δ≥1 px/fake. Criterio: si a 2 px/fake la secuencia no convence a la visión, limitar a rotaciones lentas (perfil *Superhot*) o descartar para escenas con traslación.
- **Rebanado**: si la pasada completa no se puede cortar en frontera de VBlank, el patrón no sostiene el display; el fallback honesto es declarar la cadencia real por debajo de los suelos (nunca telemetría maquillada).
- **Presupuesto Copper**: si la plantilla del horizonte no cabe o compite de más con el fetch de 5 planos, bajar a emisión/parcheo cada 2 líneas (`copper-timing-and-budget.md`).

## Demos y numeración

- Ámbito `demos/techniques/amiga/3d` (077/078/079/084 en uso; consultar `node tools/check/next-number.mjs demos/techniques/amiga/3d`): candidatas `080_camera_pan_window` (F1), `081_horizon_dpf` (F4), `082_superhot_room` (F5).
- Tests host del mapeo de ventana y de la tabla (k, S) del horizonte: reservar IDs en [`TAXONOMY.md`](../../testing/TAXONOMY.md) al crearlos.

## Referencias

- Técnicas: [`camera-pan-fake-frames.md`](../../reference/amiga/techniques/camera-pan-fake-frames.md), [`copper-horizon.md`](../../reference/amiga/techniques/copper-horizon.md), [`copper-road-rasters.md`](../../reference/amiga/techniques/copper-road-rasters.md), [`blitter-line-subpixel-fill.md`](../../reference/amiga/techniques/blitter-line-subpixel-fill.md).
- Arquitectura y estado 3D: [`3D_GAME_ARCHITECTURE.md`](../../engine/architecture/3D_GAME_ARCHITECTURE.md), [`3D_RENDER_VS_PHYSICS.md`](../../engine/architecture/3D_RENDER_VS_PHYSICS.md), [`MODEL3D_ASSET_FORMAT.md`](../../engine/architecture/MODEL3D_ASSET_FORMAT.md).
- Estado vigente: [`ROADMAP_UNIFICADO.md`](ROADMAP_UNIFICADO.md) (§subsistema poligonal y §rasterizado), [`ROADMAP_RASTER_ROAD.md`](ROADMAP_RASTER_ROAD.md), [`ROADMAP_JUEGO_SPRITES_BOBS.md`](ROADMAP_JUEGO_SPRITES_BOBS.md).
