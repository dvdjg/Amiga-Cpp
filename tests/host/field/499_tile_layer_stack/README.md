# HOST-499 — `TileLayerStack` (modos, capas vivas y prioridad)

Valida el contrato host de `eng/field/tile_layer_stack.hpp`: una pila vacía no está configurada; `baked_through` se conserva; `live_count` cuenta solo `Overlay`/`AnimatedOverlay`/`Occluder`; `is_live` no confunde `Baked`/`Skip`; y la prioridad declarada por capa llega intacta al binding.

El test no toca hardware ni `TileAnimator`: cubre la parte pura que decide qué capas entran en el pase de composición y en qué orden de degradación.
