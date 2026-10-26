# 208 - Objetos Tiled con profundidad y occluders

Demo de la capa de objetos del pipeline Tiled: la casa se cuece en la base y dos objetos se emiten en runtime. El árbol ordenado por `foot_y` sirve para comprobar el orden props/actores y la farola marcada como `occluder` se pinta después de los actores.

**Estado: VERIFICADA con excepción de rendimiento.** La composición, la oclusión y el flicker están comprobados; el frame ocupa 2 campos (24,98 fps), por debajo de 50 fps pero dentro del suelo de 25 fps para esta escena EHB de 6 planos con props y save-under. La evidencia está en [`VALIDATION.md`](VALIDATION.md).

## Cocinar los assets

```bash
node tools/tiled/cook-map.mjs \
  "../../../Assets/2D/The Fan-tasy Tileset (Free) 1.5.9/Tiled/Tilemaps/Beginning Fields.tmx" \
  --plan tools/tiled/fixtures/beginning-fields.objects.cook-plan.json \
  --out out/assets/tiled/beginning-fields-objects \
  --profiles ehb --select ehb
```

El plan cuece todo por defecto (`baked`) y deja como runtime el árbol 43 (`foreground`, a la izquierda de la casa) y la farola 143 (`occluder`). `selected/` contiene `objects.h`, `object_sheet.bin`, `object_masks.bin`, `object_pair.bin` y `objects.json`. Los props usan `object_pair.bin` (hoja par interleaved, un blit cookie-cut por objeto); el actor móvil usa la misma hoja par con save-under por Blitter.

## Compilar y ejecutar

```bash
bash tools/build/build-demo.sh demos/techniques/amiga/playfield/208_tiled_occlusion_objects --release
bash tools/run/run-demo.sh demos/techniques/amiga/playfield/208_tiled_occlusion_objects
```

La cámara fija muestra la casa, el árbol ordenado (id 43) y la farola occluder (id 143). Un explorador recorre la ruta XY y cruza árbol/farola; un segundo explorador de guardia se pinta una vez en el anillo. El orden se decide con `MapObjectsView::upper_bound_foreground(foot_y)` y los occluders se dibujan al final.

## Búsqueda previa

- `eng::map::MapObjectsView` / `MapObjectView` (`engine/include/eng/map/objects.hpp`): tabla cocinada y `upper_bound_foreground`; no existe otra vista de objetos Tiled.
- `eng::playfield::XLimitedPlayfield::add_world_bitmap_masked`: blit planar enmascarado del corkscrew; se mantiene como alternativa de actores.
- `eng::graphics::Sprite` + `BobMaskPack::InterleavedPair` + `bob_save_box`/`bob_restore_box`: hoja par de un solo blit y save-under; es el camino de props y del actor móvil, apoyado en el `BobTarget` de `scene.sprite_context()`.
- `eng::scene::BobLayer`/`BobEmitter` y `eng::scene::ActorStore`/`build_sprite_intents` se revisaron; no encajan con el orden intercalado de props/actores de esta demo sin duplicar la ordenación por `foot_y`.
- `eng::graphics::planar_patch` es 4bpp fijo; el arte del actor se cocina/empaqueta en 6 planos.
