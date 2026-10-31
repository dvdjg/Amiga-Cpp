# 208 - Objetos Tiled con profundidad y occluders

> **Cierre 2026-10-31:** 26,49 fps (suelo de 25), 1,887 campos. El actor pasa **por debajo** del cartel (foot_y > 203), no por detrás de su overlay; se elimina el parpadeo del cruce. La VISION canónica de 2 frames no era evidencia suficiente; el gate usa 40 frames y recortes.

Demo de la capa de objetos del pipeline Tiled: la casa se cuece en la base y tres objetos viven en runtime. El árbol ordenado por `foot_y` sirve para comprobar el orden props/actores; la farola y el cartel de madera marcados como `occluder` se pintan después de los actores.

**Estado:** ver ?Cierre 2026-10-31?; las cifras antiguas de 24,97 fps y 2 campos ya no aplican.

## Cocinar los assets

```bash
node tools/tiled/cook-map.mjs \
  "../../../Assets/2D/The Fan-tasy Tileset (Free) 1.5.9/Tiled/Tilemaps/Beginning Fields.tmx" \
  --plan tools/tiled/fixtures/beginning-fields.objects.cook-plan.json \
  --profiles ehb --dither none \
  --out out/assets/tiled/beginning-fields-objects --select ehb
```

La paleta la genera `cook-map` desde la composición completa del mapa (capas, animaciones y objetos) y la cuantiza a 32 bases EHB; no hay paleta fija en el repositorio.

El plan cuece todo por defecto y deja como runtime el árbol 43 (`foreground`, a la izquierda de la casa), la farola 143 (`occluder`) y el cartel 89 (`occluder`, abajo a la izquierda). `selected/` contiene `objects.h`, `object_sheet.bin`, `object_masks.bin`, `object_pair.bin` y `objects.json`. Los props usan `object_pair.bin` (hoja par interleaved, un blit cookie-cut por objeto); el actor móvil usa la misma hoja par con save-under por Blitter. La posición de los props se resuelve con `graphics/map_sprite.hpp` (`place_map_sprite` + `draw_map_sprite`): mundo → ventana → anillo, con anchor superior-izquierda y partición en la costura; la demo no repite esas cuentas.

## Compilar y ejecutar

```bash
bash tools/build/build-demo.sh demos/techniques/amiga/playfield/208_tiled_occlusion_objects --release
bash tools/run/run-demo.sh demos/techniques/amiga/playfield/208_tiled_occlusion_objects --config A500_release
```

La cámara fija (`X=200`, `Y=50`) muestra la casa, el árbol ordenado (id 43), la farola occluder (id 143) y el cartel (id 89). Un explorador recorre la ruta XY: pasa por delante del cartel, cruza detrás del árbol y pasa por detrás del guardia horneado. El guardia se repinta como overlay solo durante ese cruce; el actor nunca se oculta de golpe.

## Búsqueda previa

- `eng::map::MapObjectsView` / `MapObjectView` (`engine/include/eng/map/objects.hpp`): tabla cocinada y `upper_bound_foreground`; no existe otra vista de objetos Tiled.
- `eng::playfield::XLimitedPlayfield::add_world_bitmap_masked`: blit planar enmascarado del corkscrew; se mantiene como alternativa de actores.
- `eng::graphics::Sprite` + `BobMaskPack::InterleavedPair` + `bob_save_box`/ `bob_restore_box`: hoja par de un solo blit y save-under; es el camino de props y del actor móvil, apoyado en el `BobTarget` de `scene.sprite_context()`.
- `eng::scene::BobLayer`/`BobEmitter` y `eng::scene::ActorStore`/ `build_sprite_intents` se revisaron; no encajan con el orden intercalado de props/actores de esta demo sin duplicar la ordenación por `foot_y`.
- `eng::graphics::planar_patch` es 4bpp fijo; el arte del actor se cocina/empaqueta en 6 planos.

## Flicker de alta frecuencia

```bash
node tools/vision-review/flicker-highfreq.mjs \
  --demo demos/techniques/amiga/playfield/208_tiled_occlusion_objects \
  --config A500_release --frames 12 --require-ok
```

Captura frames consecutivos (paso 1) y ejecuta el detector de bandas alternas y el frame-diff temporal. Resultado actual: bandas OK, 0 candidatos, `blocks_low` máximo 0. El test detecta flicker de un frame de cada dos; no sustituye una captura dentro del barrido de pantalla (scanout).

## Telemetría opcional

La build normal no paga instrumentación. Para diagnosticar operaciones de Blitter y fases:

```bash
EXTRA_DEFINES="-DDEMO_BLIT_STATS=1" bash tools/build/build-demo.sh <demo> --release
EXTRA_DEFINES="-DDEMO_PROF=1" bash tools/build/build-demo.sh <demo> --release
```

`DEMO_BLIT_STATS` añade al HUD `J` (jobs), `W` (palabras) y los contadores de props/actores/occluders; `DEMO_PROF` instrumenta las secciones de update/execute/compose para `tools/debug/profile.mjs`. El código queda fuera cuando no se define, por lo que la build normal no tiene coste adicional.

La telemetría general (`-DENG_METRICS=1`) registra `object_draws`, `actor_draws`, `actor_restores` y `guard_overlays`, además de los contadores Tiled comunes. No añade coste en release y se lee por el mismo canal lateral:

```bash
EXTRA_DEFINES="-DENG_METRICS=1" bash tools/build/build-demo.sh <demo> --release
bash tools/run/run-demo.sh <demo> --config A500_eng_metrics1_release \
  --telemetry-samples 8 --telemetry-interval-ms 100
node tools/debug/metrics-report.mjs <demo> A500_eng_metrics1_release
```

El informe muestra el bloque de contadores y el anillo de eventos; el runner ya lo deja también en `run-report.json`.

- Perfil single buffer (`DEMO_PROF`, 125 frames): walker 506, restore 8.702, draw 54.552, execute 49.004, compose 33.070, HUD 37.699 ciclos/frame; total 286.276 (2,02 campos). La suma de secciones ?til es ~183.500 ciclos > 142.102 del campo. Aun eliminando el HUD (~37.7k) quedar?an ~146k, por encima del presupuesto: **no es un problema de doble buffer; el trabajo CPU/DMA de la escena no cabe en un campo**.
