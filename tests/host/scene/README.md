# Tests HOST — scene

Categoría `scene` de la batería host (L1). El índice de categorías está en [../README.md](../README.md) y la taxonomía en [docs/testing/TAXONOMY.md](../../../docs/testing/TAXONOMY.md).

## Catálogo

| ID | Test | Qué cubre |
|----|------|-----------|
| HOST-028 | [representation](028_representation/README.md) | `choose_representation`/`RepresentationAllocator`: elección de sprite/BOB/CPU/playfield por tamaño, preferencia y presupuesto, con reasignación al agotarse. |
| HOST-066 | [route_camera](066_route_camera/README.md) | `eng/scene/route_camera.hpp`: fases de la ruta, círculo sobre `radius_scale` (vía `eng::SineTable`), espejo `mirror_x` y modo salto dentro de límites. |
| HOST-072 | [actor](072_actor/README.md) | Sistema de objetos (`scene/actor.hpp`): almacén generacional, políticas de transparencia y fondo, anclaje/offset, geometría y emisión de los `BlitJob` del BOB (matriz por job), save-under por buffer, orden por superficie/`z`, sprites (intents, tiras, plantilla→intenciones, degradación a BOB) y necesidades de Copper ancladas con prioridad. Absorbe el antiguo HOST-071. |
| HOST-327 | [world](327_world/README.md) | `eng/scene/world.hpp`: mundo retenido (`World`/`Layer`) — `add_layer` (rechazo anulable), `count`/`full`, `layer`/`find` y la cámara de la capa (`scroll_x`). |
