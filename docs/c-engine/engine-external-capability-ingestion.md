# Ingesta de capacidades externas para el engine

Objetivo: capturar funcionalidad reutilizable desde repos externos y convertirla en:

- API low-level parametrica del engine
- wrapper high-level retained de escena cuando aplique
- caso de bateria con evidencia real

No es un plan de "portar demos completas". La unidad de integracion es la capacidad.

## Fuentes iniciales

- `C:/Users/dvdjg/Documents/programa/AI/Amiga-C++/demoscene-repo`
- `C:/Users/dvdjg/Documents/programa/AI/Amiga-C++/ACE`
- `C:/Users/dvdjg/Documents/programa/AI/Amiga-C++/Sevgi_Engine`
- `C:/Users/dvdjg/Documents/programa/AI/amiga-stuff`

## Contrato de ingestión por capacidad

Para cerrar una capacidad:

1. tecnica identificada y documentada (fuente + invariantes + limites)
2. primitive low-level integrada en `engine/` o validada como reusable
3. wrapper retained definido cuando aporte valor de escena
4. bateria dedicada o extendida con evidencia (`runtime-state`, captura, vision)
5. actualizacion de matriz engine <-> battery y roadmap

## Backlog inicial priorizado

| ID | Prioridad | Fuente | Capacidad a destilar | Capa objetivo | API/Modulo engine objetivo | Bateria objetivo | Estado |
|----|-----------|--------|----------------------|---------------|----------------------------|------------------|--------|
| EXTCAP-01 | P0 | amiga-stuff `scrolling_tricks/xyunlimited2` + `yunlimited2` + ACE `scrollbuffer` | scroll XY con wrap, split vertical y blits de margen | low-level + retained | `engine_view`, `engine_external_scroll`, `engine_tilemap`, `engine_copper_list` | ST01/ST02/ST03/ST04 | PARCIAL (ST01 ya usa adapter importado `engine_external_scroll`; ST02 ya existe como baseline vertical con buffer circular y split copper; ST04 ya consume adapters externos. La validacion ya exige motion por pixel y continuidad minima por caso, pero ST04 sigue bloqueado por animacion insuficiente y ST02 ha mostrado un timeout intermitente del capturador de secuencia que conviene estabilizar.) |
| EXTCAP-02 | P0 | ACE `scrollbuffer` | viewport scroll manager con cobre y punteros por frame | retained | nuevo `engine_scene_scroll` sobre `engine_view` | ST03/ST04 | PENDIENTE |
| EXTCAP-03 | P0 | ACE `tilebuffer` | invalidacion por tiles, cola de redraw y margenes | retained | nuevo `engine_scene_tilebuffer` + `engine_external_tilebuffer` | ST04 + T03(plan) | PARCIAL (helpers base + primer estado retained ya integrados en `engine_scene_tilebuffer`; ST04 ya los consume con redraw real, pero la escena todavia no demuestra scroll XY retained con continuidad visual suficiente; falta cerrar API retained publica final y rehacer la demo/evidencia de ST04) |
| EXTCAP-04 | P1 | demoscene `libblit` | copy/fill/masked/shift families con rutas especializadas | low-level | ampliar `engine/src/blitter.c` | B01/B02/B05/B06 ext | PARCIAL |
| EXTCAP-05 | P1 | demoscene `libgfx` | setup de playfield/copper mas parametrico | low-level | `engine_copper_*` y display | C02/C03/C04(plan) | PARCIAL |
| EXTCAP-06 | P1 | demoscene `sprite` | stream DMA sprite y helpers de cabecera/attach | low-level | `engine_sprite_*` | S02/S03(plan) | PARCIAL |
| EXTCAP-07 | P1 | ACE `viewport/camera` | anclaje mundo/pantalla con camara reusable | retained | `engine_scene_camera` | ST03 + CS03 ref | PENDIENTE |
| EXTCAP-08 | P1 | ACE `joy`/`key`/`mouse` | normalizacion de input y edge handling | low-level + retained | `engine_input_devices`, `engine_input_edges` | I01 + J01(plan) | PARCIAL |
| EXTCAP-09 | P1 | demoscene `libpt`/`libp61` | backend tracker para audio engine | low-level | `engine_audio` backend modular | A01/A02/A03(plan) | PENDIENTE |
| EXTCAP-10 | P1 | Sevgi `Code/tilemap/display_level` | flujo retained de escena para niveles scrollables | retained | `engine_scene_level` (wrapper) | ST04 + T07 ref | PENDIENTE |
| EXTCAP-11 | P2 | demoscene `lib2d` | clipping 2D y utilidades geometricas | low-level | `engine_bitmap`/blitter helpers | B03/B04 ext | PENDIENTE |
| EXTCAP-12 | P2 | demoscene `lib3d` | pipeline basico de transform/culling/sort | low-level + retained | modulo nuevo `engine_3d` experimental | T-3D-01(plan) | PENDIENTE |
| EXTCAP-13 | P2 | demoscene `libmisc/sync` | sincronia timeline para efectos | retained | `engine_trace`/`engine_clock` helpers | V01/V02/V03 ext | PENDIENTE |
| EXTCAP-14 | P2 | ACE `tools` | conversion pipeline assets (bitmap/font/tileset/palette) | tooling | `tools/` del repo + docs | N/A (build pipeline) | PENDIENTE |
| EXTCAP-15 | P2 | Sevgi `Tools` + editor flow | flujo de datos para tilemap/spritebank | tooling + retained | importers y contrato de datos | T03(plan) | PENDIENTE |

## Primera ola de ejecucion (sprint recomendado)

1. EXTCAP-01 (base tecnica scroll XY)
2. EXTCAP-02 (manager retained de scroll)
3. EXTCAP-03 (tilebuffer retained + invalidacion)
4. EXTCAP-04 (cerrar API blitter faltante para scroll/tile)

Resultado esperado de la primera ola:

- APIs low-level y retained minimas para escena scrollable
- ST01..ST04 como vector de cierre objetivo para la familia scroll
- base robusta para integrar tecnicas mas complejas sin duplicacion

## Politica de licencias y procedencia

- `demoscene-repo`: licencia Artistic 2.0 (ver `LICENSE.md`)
- `ACE`: MPL-2.0 (ver `LICENSE`)
- `Sevgi_Engine`: declarado MIT en README (pendiente confirmar fichero LICENSE en repo local)
- `amiga-stuff/scrolling_tricks`: sin fichero de licencia detectado en copia local (pendiente verificar origen exacto antes de copiar codigo literal)

Regla operativa:

- preferir reimplementacion guiada por tecnica y contratos
- si se copia codigo literal, registrar procedencia y obligaciones de licencia en el mismo cambio


