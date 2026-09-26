# Tests HOST — graphics

Categoría `graphics` de la batería host (L1). El índice de categorías está en [../README.md](../README.md) y la taxonomía en [docs/testing/TAXONOMY.md](../../../docs/testing/TAXONOMY.md).

## Catálogo

| ID | Test | Qué cubre |
|----|------|-----------|
| HOST-001 | [graphics_driver_contract](001_graphics_driver_contract/README.md) | Conceptos `eng::DisplayDriver` y `eng::GraphicsDriver` del ciclo de instalación del display (takeover + install): validación compile-time con `static_assert` sobre un mock, `TileScrollScene`, `XlimitedScene` y compositores DPF. |
| HOST-002 | [raster_intent](002_raster_intent/README.md) | Vocabulario portable de intenciones de display: `Visual`, `CopperIntent`, `SpriteIntent` y concept `Effect` (base de la Oleada 1 de demoscene). |
| HOST-003 | [sprite_allocator](003_sprite_allocator/README.md) | `eng::graphics::SpriteAllocator`: reparto de `SpriteIntent` entre 8 canales con multiplexado vertical y decisión overflow → BOB (paso 4 de `ENGINE_DESIGN.md` §5). |
| HOST-010 | [lib2d](010_lib2d/README.md) | `eng::retro`: primitivas 2D de lib2d sobre los tipos genéricos (Vec2/Mat2x2/Rect + recorte). |
| HOST-011 | [math3d](011_math3d/README.md) | `eng::math3d`: primitivas 3D (port de `lib3d`) — matrices, rotaciones, proyección. |
| HOST-013 | [math3d_mesh](013_math3d_mesh/README.md) | `eng::math3d` mesh: `mesh_transform`, `mesh_painter_order` (culling + painter). |
| HOST-015 | [fire_sim](015_fire_sim/README.md) | Fuego de `fire-rgb`: simulación (abajo caliente) + `dualtab` C++23 `constexpr` verificado contra el original. |
| HOST-016 | [ham_scene](016_ham_scene/README.md) | `eng/graphics/composition/compose.hpp`: display planar con repetición de filas (cuadruplicado) + huella estática `row_repeat_words` verificada contra la emisión real — geometría de la copperlist y parametricidad. |
| HOST-019 | [copper_ext](019_copper_ext/README.md) | Extensiones de Copper para *copper chunky*: `SKIP` (máscara `0xffff`), `MOVE32` (orden `reg+2`/`reg`) y parcheo de instrucciones (`move_at`/`patch_data`). |
| HOST-027 | [animation](027_animation/README.md) | `Animation`/`Frame`: avance determinista por ticks de juego, loop/one-shot, desacoplado de la representación. |
| HOST-042 | [mode_switch](042_mode_switch/README.md) | `ModeSwitchZone`: conmutación de geometría de vídeo a mitad de frame con el orden canónico MI09 (`BPLCON0`→`DDF`→mods→`BPLxPT`). |
| HOST-046 | [mesh_renderer](046_mesh_renderer/README.md) | `eng::graphics::mesh_renderer`: malla 3D → `Surface` (proyección, culling, relleno). |
| HOST-047 | [lib3d](047_lib3d/README.md) | `eng::lib3d`: visibilidad de caras/aristas + transform+proyección sobre el modelo empaquetado de `object3d`. |
| HOST-050 | [math3d_exact](050_math3d_exact/README.md) | Bit-exactitud de `math3d` (4096 ángulos, load_rotate + reverse). |
| HOST-053 | [lib3d_projection](053_lib3d_projection/README.md) | Tabla dorada de la proyección `lib3d::transform_vertices` a ángulo fijo. |
| HOST-067 | [multi_buffered](067_multi_buffered/README.md) | `scene::compose` con `SceneResources.buffers`: N buffers de display, `commit()` (publica y rota) y parcheo de `BPLxPT` sin dueño de memoria externa. |
| HOST-069 | [copper_double_buffer](069_copper_double_buffer/README.md) | `eng::copper::DoubleBuffer`: 2 bloques de copperlist, `flip`, publicar; y `TileScrollScene` parcheando el bloque inactivo. |
| HOST-070 | [copper_plan](070_copper_plan/README.md) | `eng::copper::Plan`: orden por scanline, **prioridad `(superficie, z)`**, doble buffer de copperlist, publicación y rechazo por overflow. |
| HOST-105 | [sprite_hrearm](105_sprite_hrearm/README.md) | Multiplexado **horizontal** de sprites (`Scheduler::emit_sprite_horizontal_rearm(s)`/`reposition` + `graphics::SpriteHorizontalRearm`): codificación AHRM de `SPRxPOS`/`SPRxCTL`, secuencia WAIT+POS+CTL+DATA+DATB sin tocar `SPRxPT`, y orden por `hpos` de la lista. |
| HOST-132 | [rotozoom](132_rotozoom/README.md) | `graphics/effects/rotozoom.hpp`: muestreo por píxel con aritmética entera (tabla de seno 16.16, identidad, rotación, zoom). |
| HOST-133 | [palette_transition](133_palette_transition/README.md) | `graphics/effects/palette_transition.hpp`: estado `num/den` (una pasada y vaivén), paleta runtime y parche base/zona en `FramePlan`. |
| HOST-134 | [raster_gradient](134_raster_gradient/README.md) | `graphics/effects/raster_gradient.hpp`: muestreo de claves (lineal/cíclico, `phase`), geometría de bandas e intenciones `PaletteLine`. |
| HOST-178 | [dna_helix](178_dna_helix/README.md) | Port de `GenCircularDoubleHelix` de `dna3d` a `Turns`/`sin`/`cos`, verificado punto a punto contra la transcripción del original (`kSinTab`) e invariantes de geometría. |
| HOST-211 | [mesh_poly_render](211_mesh_poly_render/README.md) | `eng/graphics/mesh_renderer.hpp::mesh_render_poly_filled`: malla n-gon (transform + culling por normal + `Surface::fill_polygon`) con `project_perspective` genérica sobre el escalar. |
| HOST-212 | [canvas_scene](212_canvas_scene/README.md) | `eng/graphics/composition/compose.hpp`: `field::Surface` sobre escena en layout **interleaved** y **contiguo** (`ContiguousPlayfield`): `fill_polygon`/`draw_line`/`blit` y doble buffer. |
| HOST-213 | [outline_xor](213_outline_xor/README.md) | `eng/retro/flat_shade_xor.hpp`: técnica Amiga de contorno EOR + área fill XOR (secuencia de Blitter, horizontales descartadas, un fill). |
| HOST-214 | [copper_patch](214_copper_patch/README.md) | `eng/copper::PatchHandle` (`scheduler.hpp`): MOVE parcheable por frame (precisión quirúrgica en el modelo de composición de escenas). |
| HOST-215 | [scene_compose](215_scene_compose/README.md) | `eng/graphics/composition/compose.hpp`: escena planar por etapas (display + paleta + etapa propia con `PatchHandle`); presets en vez de clases por driver. |
| HOST-216 | [scene_display_limits](216_scene_display_limits/README.md) | `eng/graphics/composition/limits.hpp`: perfiles OCS/ECS/AGA, validación `validate`/`valid_scene` y coste de bus `dma_cost` (fetch 1×/2×/4×, slots 226/27). |
| HOST-217 | [polygon_planes](217_polygon_planes/README.md) | `eng/graphics/polygon_planes.hpp`: relleno de polígonos **compuesto por bitplane** (CPU): un fill por plano según los bits del color, con cancelación de aristas compartidas (even-odd). |
| HOST-218 | [seam_c2p](218_seam_c2p/README.md) | `Rasterizer::c2p` (seam): chunky→planar unificado (CPU vía `c2p_1x1_4`/`naive`), comparado con la referencia. |
| HOST-260 | [copper_blitter](260_copper_blitter/README.md) | Copper lanza blits (Técnica A): `CopperIntentKind::BlitterJob` (`BLTCON*`/punteros/módulos/`BLTSIZE`) y **ventana segura** (`set_blitter_window`) que lo serializa con los blits de CPU. |
| HOST-267 | [fine_scroll](267_fine_scroll/README.md) | `eng::effects::FineScroll`: scroll horizontal fino de una capa planar (1 px/frame) — `step`/`bplcon1`/columna y `BlitJob` de desplazamiento + columna entrante. |
| HOST-323 | [palette](323_palette/README.md) | `eng/graphics/palette.hpp`: paleta de juego (`Palette`/`Color`/`ColorIndex`) — `set`/`get`/`fill`/`fade`/`mix` (`constexpr`), índice recortado a 0..31 y parche base al `FramePlan`. |
| HOST-324 | [sprite](324_sprite/README.md) | `eng/graphics/sprite_asset.hpp`: sprite de juego (`Sprite` sobre `graphics::bob`) — geometría, `draw`/`erase` y los `BlitJob`s (planar cookie-cut/OR, interleaved OR, borrado y rechazos). |
| HOST-338 | [chr](338_chr/README.md) | `eng/graphics/chr.hpp`: decode **2bpp → planar** (`chr_to_planar`) — helper para `IPatternCache`/sprite de consumidores externos. |
| HOST-342 | [tile_editor](342_tile_editor/README.md) | `eng/graphics/tilemap/tile_editor.hpp`: editor de tiles de juego (`set_tile`/`dirty_rect`/`flush` sobre `PackedTileCell`) — `IScrollingLayer` (F7.2). |
| HOST-344 | [attribute_table](344_attribute_table/README.md) | `eng/graphics/tilemap/attribute_table.hpp`: paleta por bloque (`set`/`get`/`fill`) — `set_attribute` de una capa de tiles (F7.2). |
