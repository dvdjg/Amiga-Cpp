# Indice de cobertura demoscene-repo -> Cursor-Amiga-C

Indice operativo para seguir la importacion de efectos desde:

- `C:\Users\dvdjg\Documents\programa\AI\Amiga-C++\demoscene-repo\effects`
- `C:\Users\dvdjg\Documents\programa\AI\Amiga-C++\demoscene-repo\docs\tutoriales`

Este documento convierte el roadmap general en una tabla de trabajo mantenible. Cada fila representa un efecto origen y su estado dentro de Cursor-Amiga-C.

## Estados sugeridos

| Estado | Significado |
|--------|-------------|
| `NO_INICIADO` | Aun no se ha auditado ni creado caso local. |
| `AUDITADO` | Tutorial y codigo origen revisados; tecnica y dependencias identificadas. |
| `IMPORTANDO` | Existe trabajo activo de adaptacion al caso local. |
| `CASO_BATERIA` | Existe caso local de bateria con documentacion base. |
| `VALIDADO` | El caso local tiene evidencia viva suficiente. |
| `ENGINE_PARCIAL` | Parte reusable ya ha subido al engine, pero aun queda logica local o API inestable. |
| `ENGINE_HECHO` | La parte reusable ya converge claramente a `engine/` y el caso la consume. |
| `REFERENCIA_SOLO` | Se conserva como referencia o fuente de investigacion, sin promocion inmediata al engine. |

## Columnas de trabajo

- **Origen**: tutorial y carpeta del repositorio demoscene.
- **Estado local**: una de las etiquetas anteriores.
- **Caso bateria destino**: carpeta actual o prevista en `tests/amiga-battery/`.
- **API engine candidata**: modulo o familia `engine_*` que podria absorber la tecnica.
- **Notas de adaptacion**: restricciones, chipset, acoplamientos o aclaraciones.

## Cobertura actual

| # | Efecto | Carpeta origen | Estado local | Caso bateria destino | API engine candidata | Notas de adaptacion |
|---|--------|----------------|--------------|----------------------|----------------------|---------------------|
| 01 | Empty | `effects/empty` | `VALIDADO` | `DX01_empty_import` | plantilla de efecto / estado | Caso local ya validado por ADF con `assert_failures=0`, `stage_id=0xD104` y captura/vision coherentes; sirve como baseline real de importacion desde `demoscene-repo`. |
| 02 | Circles | `effects/circles` | `NO_INICIADO` | `DX02_circles_cpu` | `engine_draw_circle_*` | Caso CPU simple; buena referencia para coste por frame y para distinguir demo/test de runtime real. |
| 03 | Color-cycling | `effects/color-cycling` | `CASO_BATERIA` | `DX03_color_cycle_logo` | `engine_palette_cycle_*` | Caso local ya compila, arranca y valida imagen 1:1; promotion parcial al engine hecha, pero la evidencia temporal APNG sigue pendiente porque el pipeline no detecta aun el cambio de paleta entre frames. |
| 04 | Plasma | `effects/plasma` | `NO_INICIADO` | `DX04_plasma_copper` | `engine_copper_line_bands_*` | Requiere describir muy bien copper por tile/banda y coste CPU real. |
| 05 | Fire RGB | `effects/fire-rgb` | `NO_INICIADO` | `DX05_fire_rgb` | `engine_c2p_*` / `engine_blit_*` | Tecnica compuesta; mejor dejarla para oleada tardia. |
| 06 | Wireframe | `effects/wireframe` | `NO_INICIADO` | `DX06_wireframe` | `engine_3d_*` / `engine_blit_line_*` | Muy util para futura capa 3D del engine. |
| 07 | Shapes | `effects/shapes` | `NO_INICIADO` | `DX07_shapes_fill` | `engine_2d_*` / `engine_blit_fill_*` | Puede converger con B03/B04 y con libs 2D. |
| 08 | Floor | `effects/floor` | `NO_INICIADO` | `DX08_floor_scroll` | `engine_scroll_*` / `engine_copper_*` | Muy interesante para scroll y `BPLCON1` por linea. |
| 09 | TextScroll | `effects/textscroll` | `NO_INICIADO` | `DX09_textscroll` | `engine_textscroll_*` | Alta prioridad para texto y copper `bplpt` por linea. |
| 10 | Loader | `effects/loader` | `CASO_BATERIA` | `DX10_loader_import` | loader / dev harness / `engine_loader_screen_*` | Fase 1 ya importada: ilustracion y barra de progreso funcionan en vivo con `stage_id=0xDA04`; audio tracker sigue diferido y la evidencia temporal APNG sigue pendiente por el pipeline de secuencias. |
| 11 | Game of Life | `effects/game-of-life` | `NO_INICIADO` | `DX11_game_of_life` | `engine_blit_minterm_*` | Muy buen caso de blitter creativo. |
| 12 | Stripes | `effects/stripes` | `NO_INICIADO` | `DX12_stripes_copper` | `engine_copper_color_bands_*` | Caso pequeno de copper por linea. |
| 13 | Highway | `effects/highway` | `NO_INICIADO` | `DX13_highway` | scroll/zonas/sprites | Tecnica compuesta; no entrar temprano. |
| 14 | Metaballs | `effects/metaballs` | `NO_INICIADO` | `DX14_metaballs` | blob/blit helpers | Buena referencia de mascaras y doble buffer. |
| 15 | Indice resto | `docs/tutoriales/15-indice-resto.md` | `REFERENCIA_SOLO` | N/A | N/A | Documento indice, no efecto. |
| 16 | Abduction | `effects/abduction` | `NO_INICIADO` | `DX16_abduction` | anim/layers helpers | Posible referencia de animacion por frames. |
| 17 | Anim | `effects/anim` | `NO_INICIADO` | `DX17_anim_frames` | `engine_anim_*` | Importante para secuencias prerenderizadas. |
| 18 | Anim-polygons | `effects/anim-polygons` | `NO_INICIADO` | `DX18_anim_polygons` | import pipeline / 2D geom | Buen puente hacia 2D mas declarativo. |
| 19 | Ball | `effects/ball` | `NO_INICIADO` | `DX19_ball` | blit runtime helpers | Caso pequeno de movimiento y render por frame. |
| 20 | Blurred | `effects/blurred` | `NO_INICIADO` | `DX20_blurred` | blur buffer ops | Coste por frame importante; documentar bien CPU/blitter. |
| 21 | Blurred3D | `effects/blurred3d` | `NO_INICIADO` | `DX21_blurred3d` | 3D + accumulation | Mejor tras wireframe/flatshade. |
| 22 | Bobs3D | `effects/bobs3d` | `NO_INICIADO` | `DX22_bobs3d` | object/scene helpers | Mezcla 3D con BOBs; no abrir sin base fuerte. |
| 23 | Bumpmap RGB | `effects/bumpmap-rgb` | `NO_INICIADO` | `DX23_bumpmap_rgb` | lighting helpers | Tardia por coste y formato. |
| 24 | Butterfly-gears | `effects/butterfly-gears` | `NO_INICIADO` | `DX24_butterfly_gears` | loops especializados | Buena referencia de optimizacion micro. |
| 25 | Carrion | `effects/carrion` | `NO_INICIADO` | `DX25_carrion` | blit por trozos | Caso muy especifico. |
| 26 | Cathedral | `effects/cathedral` | `NO_INICIADO` | `DX26_cathedral` | ray casting + copper | Avanzado; mejor despues. |
| 27 | Credits | `effects/credits` | `NO_INICIADO` | `DX27_credits` | texto/paleta | Caso facil de UI/texto. |
| 28 | Darkroom | `effects/darkroom` | `NO_INICIADO` | `DX28_darkroom` | palette LUT helpers | Bueno para fades y LUTs. |
| 29 | Dna3D | `effects/dna3d` | `NO_INICIADO` | `DX29_dna3d` | `engine_3d_*` + paleta | Puede servir para profundidad por paleta. |
| 30 | FlatShade | `effects/flatshade` | `NO_INICIADO` | `DX30_flatshade` | polyfill / 3D fill | Alta prioridad dentro de la oleada 3D. |
| 31 | FlatShade-convex | `effects/flatshade-convex` | `NO_INICIADO` | `DX31_flatshade_convex` | convex fill helpers | Variante util para API especializada. |
| 32 | Floor-old | `effects/floor-old` | `NO_INICIADO` | `DX32_floor_old` | scroll comparisons | Referencia secundaria frente a `08-floor`. |
| 33 | Forest | `effects/forest` | `NO_INICIADO` | `DX33_forest` | scene manager hints | Muy compuesto; dejar tardio. |
| 34 | Glitch | `effects/glitch` | `NO_INICIADO` | `DX34_glitch` | debug/guardrails | Util como caso de robustez, no de engine base. |
| 35 | Glitches | `effects/glitches` | `NO_INICIADO` | `DX35_glitches` | debug/guardrails | Similar al anterior. |
| 36 | Growing-tree | `effects/growing-tree` | `NO_INICIADO` | `DX36_growing_tree` | line helpers | Puede converger con lineas y L-systems. |
| 37 | GUI | `effects/gui` | `NO_INICIADO` | `DX37_gui_import` | `engine_ui_*` | Alta prioridad como referencia de UI. |
| 38 | Kbtest | `effects/kbtest` | `NO_INICIADO` | `DX38_kbtest` | `engine_input_*` | Alta prioridad por input y sistema. |
| 39 | Layers | `effects/layers` | `ENGINE_PARCIAL` | `DX39_layers_dualpf` + `DX39P0_dualpf_synthetic_static` + `DX39P1_layers_static_dualpf` + `DX39P2_background_singlepf` + `DX39P3_foreground_singlepf` + `DX39P4_layers_scroll_no_raster` + `DX39P5_layers_wrap_mod` + `DX39P6_layers_raster_gradients` | `engine_copper_setup_display_mode_ocs`, `engine_copper_set_dual_playfield_bitmap_views`, `engine_copper_set_planar_bitmap_view`, `engine_alloc_chip_copy` | Rebuild por fases ya muy avanzado. `DX39P0` demuestra que la ruta reusable del engine para dual playfield funciona con bitmaps sinteticos. El fallo de `DX39P1/P2/P3` no estaba en el dual playfield ni en el copiado de ficheros: los SHA-256 coinciden con el origen. La causa raiz confirmada era que los bitplanes importados no estaban garantizados en CHIP. Tras clonarlos con `engine_alloc_chip_copy`, `DX39P2` y `DX39P3` vuelven a verse bien y `DX39P1` recupera la composicion estatica correcta. `DX39P4` valida scroll por frame con `BPLxPT + BPLCON1`; `DX39P5` anade wrap vertical por `BPL1MOD/BPL2MOD` dinamicos; `DX39P6` reintroduce ya el color por raster con rampa suavizada linea a linea y difusion de error vertical en `COLOR01..06` y `COLOR09..13` sobre la misma base saneada. |
| 40 | Lines | `effects/lines` | `NO_INICIADO` | `DX40_lines` | `engine_blit_line_*` | Util para especializaciones de dibujo. |
| 41 | Magnifying-glass | `effects/magnifying-glass` | `NO_INICIADO` | `DX41_magnifying_glass` | zoom/copy helpers | Caso de copia y mascara interesante. |
| 42 | MultiPipe | `effects/multipipe` | `NO_INICIADO` | `DX42_multipipe` | 3D fill pipeline | Tardia. |
| 43 | Neons | `effects/neons` | `NO_INICIADO` | `DX43_neons` | palette cycling / layers | Buen caso visual 2D. |
| 44 | PlayAHX | `effects/playahx` | `NO_INICIADO` | `DX44_playahx` | `engine_audio_ahx_*` | Prioridad media; requiere pipeline de audio. |
| 45 | PlayCinter | `effects/playctr` | `NO_INICIADO` | `DX45_playcinter` | `engine_audio_cinter_*` | Carpeta origen distinta del nombre del tutorial. |
| 46 | PlayP61 | `effects/playp61` | `NO_INICIADO` | `DX46_playp61` | `engine_audio_p61_*` | Alta prioridad de audio por cercania con el repo actual. |
| 47 | PlayProtracker | `effects/playpt` | `NO_INICIADO` | `DX47_playprotracker` | `engine_audio_pt_*` | Carpeta origen distinta del nombre del tutorial. |
| 48 | Plotter | `effects/plotter` | `NO_INICIADO` | `DX48_plotter` | plotter/line helpers | Caso simple de puntos/segmentos. |
| 49 | Prisms | `effects/prisms` | `NO_INICIADO` | `DX49_prisms` | mesh fill | Bueno para triangulos 3D. |
| 50 | Roller | `effects/roller` | `NO_INICIADO` | `DX50_roller` | scanline `bplpt` helpers | Muy interesante para copper dinamico. |
| 51 | Rotator | `effects/rotator` | `NO_INICIADO` | `DX51_rotator` | rotator helpers | Caso 2D/3D intermedio. |
| 52 | Sea-anemone | `effects/sea-anemone` | `NO_INICIADO` | `DX52_sea_anemone` | fx tables + line helpers | Buen caso de lineas radiales. |
| 53 | ShowPCHG | `effects/showpchg` | `NO_INICIADO` | `DX53_showpchg` | per-line palette helpers | Alta prioridad para palette change. |
| 54 | Spooky-tree | `effects/spooky-tree` | `NO_INICIADO` | `DX54_spooky_tree` | line/fade helpers | Tardio. |
| 55 | Stencil3D | `effects/stencil3d` | `NO_INICIADO` | `DX55_stencil3d` | minterm 3D masking | Avanzado pero valioso. |
| 56 | TexObj | `effects/texobj` | `NO_INICIADO` | `DX56_texobj` | `engine_uv_*` | Muy util para futuro texturizado. |
| 57 | Thunders | `effects/thunders` | `NO_INICIADO` | `DX57_thunders` | line/fill FX | Caso compuesto 2D interesante. |
| 58 | Tiles8 | `effects/tiles8` | `VALIDADO` | `DX58P1_tiles8_map` + `DX58P2_tiles8_copper_color` + `DX58P3_tiles8_blitter_render` | `engine_tiles8_*` | `DX58P1` valida el nucleo reutilizable: tilemap `8x8` monocromo `304x256`, atlas importado, mapa `UWORD`, doble buffer y animacion por frame con `stage_id=0xD584` y `assert_failures=0`. `DX58P2` anade color por copper con lista preconstruida/parcheada (`stage_id=0xD594`). `DX58P3` mantiene esa copper y sustituye el redraw CPU por una ruta de blitter inspirada en `RenderTiles()` del original; queda validado por ADF con `stage_id=0xD5A4`, `assert_failures=0`, bitplane en CHIP y una actividad de frame claramente superior a `DX58P2` (`write_index=236` frente a `122`). |
| 59 | Tiles16 | `effects/tiles16` | `ENGINE_PARCIAL` | `DX59_tiles16` + `DX59P2_tiles16_scroll_bidir` | `engine_tiles16_*`, `engine_tilemap_*` | `DX59` queda validado como import base (`stage_id=0xD5B4`). `DX59P2` queda estabilizado con doble buffer, scroll horizontal bidireccional y presupuesto de blits por frame, validado por ADF + evidencia interna 1:1 + secuencia. Parte reusable promovida al engine: `engine_tilemap_mark_scroll_h_dirty`, `engine_tilemap_mark_scroll_xy_dirty` y `engine_tilemap_next_dirty_cell` (agnosticas de bitplanes), dejando base para el siguiente caso omnidireccional. |
| 60 | TileZoomer | `effects/tilezoomer` | `NO_INICIADO` | `DX60_tilezoomer` | tile zoom helpers | Mejor despues de tiles base. |
| 61 | Transparency | `effects/transparency` | `NO_INICIADO` | `DX61_transparency` | transparency/minterm API | Alta prioridad media. |
| 62 | Turmite | `effects/turmite` | `NO_INICIADO` | `DX62_turmite` | CPU buffer policies | Caso CPU interesante, no prioritario. |
| 63 | Twister RGB | `effects/twister-rgb` | `NO_INICIADO` | `DX63_twister_rgb` | RGB/AGA path | Tratar como avanzado/chipset-dependiente. |
| 64 | UVLight | `effects/uvlight` | `NO_INICIADO` | `DX64_uvlight` | UV palette helpers | Ligado a paleta/UV. |
| 65 | UVMap | `effects/uvmap` | `NO_INICIADO` | `DX65_uvmap` | `engine_uv_*` | Muy util para futuro motor visual. |
| 66 | UVMapRGB | `effects/uvmap-rgb` | `NO_INICIADO` | `DX66_uvmap_rgb` | RGB/AGA UV path | Avanzado por formato/color. |
| 67 | Weave | `effects/weave` | `NO_INICIADO` | `DX67_weave` | painter ordering / blit | Buen caso 2D tardio. |
| EX | Starfox | `effects/starfox` | `NO_INICIADO` | `DXEX_starfox` | por decidir | Existe en `effects/` pero no aparece en el indice de tutoriales consultado; tratar aparte. |

## Primer lote recomendado

| Orden | Efecto | Motivo |
|------|--------|--------|
| 1 | `01-empty` | Fija esqueleto de efecto importado y convención de adaptación. |
| 2 | `10-loader` | Alinea la ruta de carga con un caso funcional de origen. |
| 3 | `03-color-cycling` | Técnica simple, barata y altamente reusable. |
| 4 | `39-layers` | Da una base fuerte para dual playfield y prioridades visuales. |
| 5 | `58-tiles8` | Aporta valor directo para juegos 2D y futura cámara/scroll. |
| 6 | `46-playp61` | Introduce el bloque de audio con una referencia funcional concreta. |

## Reglas de mantenimiento

- actualizar el estado de una fila cuando exista un cambio real en el repo;
- enlazar el caso de bateria real en cuanto se cree;
- si una tecnica ya sube al engine, anotar la API exacta y el estado `ENGINE_PARCIAL` o `ENGINE_HECHO`;
- si un efecto queda como referencia sin promocion inmediata, marcarlo como `REFERENCIA_SOLO` en lugar de forzar una API prematura.
