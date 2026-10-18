# Tests HOST — adv

Categoría `adv` de la batería host (L1). El índice de categorías está en [../README.md](../README.md) y la taxonomía en [docs/testing/TAXONOMY.md](../../../docs/testing/TAXONOMY.md).

Cubre la **capa de contenido de la aventura gráfica** (`engine/include/eng/adv/`): walkboxes, hotspots y vista de sala — el destino del `SIM v2` del pipeline de importación. Diseño: [ADVENTURE_LAYER.md](../../../docs/engine/architecture/ADVENTURE_LAYER.md); plan: [ROADMAP_ADVENTURE.md](../../../docs/guides/roadmap/ROADMAP_ADVENTURE.md) e [importación](../../../docs/guides/roadmap/ROADMAP_ADVENTURE_IMPORT.md).

## Catálogo

| ID | Test | Qué cubre |
|----|------|-----------|
| HOST-473 | [adv_core](473_adv_core/README.md) | `eng/adv/{types,walkbox,hotspot,room}.hpp`: cajas convexas (validez con centinela SCUMM, `contains` con cualquier giro, `closest_point` de borde), `box_at` (última caja gana, centinelas saltados), hotspots con `hit_test` (último gana, bordes incluidos) y `RoomContent::ok()` — con **dos escalares** (`s16` y `float`). |
| HOST-475 | [adv_navigation_camera](475_adv_navigation_camera/README.md) | eng/adv/{navigation,camera}.hpp (A0): ruta por walkboxes con adyacencia geometrica y tolerancia, suavizado por visibilidad; camara de mundo con dead-zone, clamp y paneo temporizado con easing. Con s16/loat/Fixed. |
