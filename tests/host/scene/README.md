# Tests HOST — scene

Categoría `scene` de la batería host (L1). El índice de categorías está en [../README.md](../README.md) y la taxonomía en [docs/testing/TAXONOMY.md](../../../docs/testing/TAXONOMY.md).

## Catálogo

| ID | Test | Qué cubre |
|----|------|-----------|
| HOST-028 | [representation](028_representation/README.md) | `choose_representation`/`RepresentationAllocator`: elección de sprite/BOB/CPU/playfield por tamaño, preferencia y presupuesto, con reasignación al agotarse. |
| HOST-066 | [route_camera](066_route_camera/README.md) | `eng/scene/route_camera.hpp`: fases de la ruta, círculo sobre `radius_scale` (vía `eng::SineTable`), espejo `mirror_x` y modo salto dentro de límites. |
| HOST-072 | [actor](072_actor/README.md) | Sistema de objetos (`scene/actor.hpp`): almacén generacional, políticas de transparencia y fondo, anclaje/offset, geometría y emisión de los `BlitJob` del BOB (matriz por job), save-under por buffer, orden por superficie/`z`, sprites (intents, tiras, plantilla→intenciones, degradación a BOB) y necesidades de Copper ancladas con prioridad. Absorbe el antiguo HOST-071. |
| HOST-327 | [world](327_world/README.md) | `eng/scene/world.hpp`: mundo retenido (`World`/`Layer`) — `add_layer` (rechazo anulable), `count`/`full`, `layer`/`find` y la cámara de la capa (`scroll_x`). |
| HOST-329 | [world_actors](329_world_actors/README.md) | `eng/scene/world.hpp` (actores): `reset_actors`/`add_actor` con representación elegida, acceso por `ActorId` y `emit` al `FramePlan` (jobs de BOB). |
| HOST-335 | [sprite_actor](335_sprite_actor/README.md) | Descriptor único de objeto: `Sprite`↔`Visual`/`ActorDesc` (`actor_desc_from_sprite`); `Sprite` declara `sheet_bytes`/`mask_bytes` y `Visual` transporta frames. |
| HOST-336 | [world_layers](336_world_layers/README.md) | `eng/scene/world.hpp`: contenido de capa (actores vs **tilemap** vía `TileLayer`), `add_tile_layer`/`kind`/`is_tilemap` (F4b). |
| HOST-337 | [layer_plan](337_layer_plan/README.md) | `eng/scene/world.hpp`: regiones con **técnica genérica** (modo `SceneMode` × scroll `ScrollKind` + `region_cost`) y capa declarativa; «la capa pide, el planner dispone» (F4c-modelo). |
| HOST-343 | [scroll_plan](343_scroll_plan/README.md) | `eng/scene/scroll_plan.hpp`: scroll adaptativo — degrada `CopperSplit→CopperRing→Fine` por Copper y estima memoria de la ventana (F7.3). |
| HOST-345 | [region_plan](345_region_plan/README.md) | `eng/scene/scroll_plan.hpp`: `plan_region` — elige el scroll efectivo de una `WorldRegion` por presupuesto (Copper/Chip) con coste y memoria (F4c/decisión). |
