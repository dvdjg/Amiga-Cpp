# VALIDATION - 208_tiled_occlusion_objects

## Cierre 2026-10-31

- Perfil single buffer (`DEMO_PROF`, 125 frames): walker 506, restore 8.702, draw 54.552, execute 49.004, compose 33.070, HUD 37.699 ciclos/frame; total 286.276 (2,02 campos). La suma de secciones ?til es ~183.500 ciclos > 142.102 del campo. Aun eliminando el HUD (~37.7k) quedar?an ~146k, por encima del presupuesto: **no es un problema de doble buffer; el trabajo CPU/DMA de la escena no cabe en un campo**.
- `A500_release`: **26,49 fps**, 267.767 ciclos/frame, 1,887 campos/frame; suelo de 25.
- La ruta del actor pasa por debajo del cartel; el overlay del cartel ya no se dispara en el descenso. Secuencia de 40 frames y recortes: el actor no desaparece.
- `flicker-highfreq`: `bands=ok`, `candidates=0`, `max_blocks_low=2`.
- La VISION canónica anterior usó **2 frames**, describió la escena como estática y no vio el cruce; se conserva solo como aviso, no como validación.

**Estado:** ver ?Cierre 2026-10-31?; las cifras antiguas de 24,97 fps y 2 campos ya no aplican.

## Evidencia host

- `node tools/tiled/test-cook-plan.mjs` → `OK: cook-plan contract` (reglas por `id`/`gid`/`class`, `sort_y`, `occluder`).
- `node tools/tiled/test-cook.mjs` → 5/5 OK; los objetos runtime emiten `objects.h`, `object_sheet.bin`, `object_masks.bin` y `object_pair.bin`.
- `node tools/tiled/test-parse-tmx.mjs` → 17/17 OK.
- Cocción EHB de `Beginning Fields` con `beginning-fields.objects.cook-plan.json`: 1 foreground + 2 occluders, 3 imágenes; `object_sheet.bin` 8.772 B, `object_masks.bin` 1.462 B, `object_pair.bin` 27.072 B. La paleta se genera por mapa desde `palette-source.png` (todas las capas/objetos) y se cuantiza con el nuevo seed farthest-first.
- `objects.json`: id 43 `{gid:514, x:226, y:27, w:64, h:63, foot_y:90}` foreground; id 143 `{gid:490, x:224.5, y:60.25, w:46, h:62, foot_y:122.25}` occluder; id 89 `{gid:486, x:246, y:161, w:44, h:42, foot_y:203}` occluder (cartel).

## Evidencia Amiga

- Build release: OK (`208_tiled_occlusion_objects.A500_release.exe`).
- `run-demo`: READY por canal lateral, sin `FAILED`; cámara `X=200`, `Y=50`.
- Props por hoja par interleaved (`Sprite` + `BobMaskPack::InterleavedPair`): árbol id 43, farola id 143 y cartel id 89 visibles con la máscara correcta. El cartel se hornea una vez. La posición se resuelve con `graphics/map_sprite.hpp` (`place_map_sprite` + `draw_map_sprite`) y el anillo parte los props en la costura.
- Actor móvil por hoja par y save-under por Blitter sobre el anillo; el explorador de guardia se dibuja una vez en el init y se repinta como overlay solo cuando el móvil cruza por detrás (una pasada, sin save-under).
- La ruta del móvil cruza **por delante del cartel** (pie > 203) y **por detrás del guardia** (pie 184 en el tramo `x=300`). No hay ocultación binaria del actor ni salto de visibilidad.
- Secuencia de 40 frames cada 120 ms: el actor permanece visible; no aparece ni desaparece de golpe. La respuesta cruda por entidad y la comparación con el frame-diff están en `208_tiled_occlusion_objects_report.md` y `src/VISION.md`.
- Telemetría `-DENG_METRICS=1` (30 muestras, 300 ms): `actor_draws=1` y `actor_restores=1` mientras el móvil está en pantalla; el anillo registra `guard_overlays=1` en los frames f124..f139, que corresponde al cruce por detrás del guardia. No hay frames con el actor ausente sin una restauración/overlay asociado.

## Rendimiento

- `node tools/debug/measure-fps.mjs 208_tiled_occlusion_objects A500_release --json` → **24,97 fps**, **284.036 ciclos/frame**, **2,002 campos/frame**; ventana estable 24,76-25,16 fps, cuantización a 2 campos.
- El coste se mantiene en el suelo de 25 fps al no repintar el cartel completo: el cartel está horneado y el guardia solo cuesta un overlay en su cruce.

## Flicker de alta frecuencia

- `node tools/vision-review/flicker-highfreq.mjs --demo ... --no-run --frames 12 --require-ok` → `bands=ok`, `candidates=0`, `max_blocks_low=0`.
- Informe: `out/vision-review/208_tiled_occlusion_objects/flicker-highfreq.md`.
- El test captura frames consecutivos (paso 1) para no perder una alternancia de un frame de cada dos. No captura dentro del scanout; si se sospecha tearing de buffer único, el siguiente paso es doble buffer, no relajar el umbral.

## Visión

- Se grabaron 40 frames cada 120 ms en `out/run/208_tiled_occlusion_objects/A500_release/sequence/` y un recorte 2x de la zona cartel/guardia en `out/tmp/208_entity_seq/`.
- La pasada canónica (`vision-final`, 8 frames) detecta al explorador de camisa roja y pantalón azul moviéndose; la pasada por entidades (`vision-run`, 10 frames) confirma que **no aparece ni desaparece de golpe**. El modelo sigue siendo un filtro de sospecha: la oclusión del guardia se apoya en el frame-diff y en los eventos de telemetría.
- Gate objetivo: `frame-diff` del recorte mide bboxes de cambio que avanzan de `x=88..119` a `x=420..451` entre pares consecutivos; por tanto el movimiento está confirmado y la parte «estático» del modelo queda refutada. La desaparición abrupta no se observa; la ocultación del guardia está registrada por los eventos de telemetría.
- Queda pendiente una ventana de frames **consecutivos** que cubra el cruce completo del guardia y el recorte del actor en el instante exacto del overlay; no se declara cubierto por el modelo.

## Deuda

- El cartel id 89 está horneado y el actor lo cruza por delante; no se ejerce el cruce por detrás. Falta una oclusión parcial barata (recorte del cartel) o una ruta alternativa que no produzca el pop binario anterior.
- La pasada de visión por entidad confirma ausencia de aparición/desaparición abrupta, pero no cubre el cruce completo del guardia con frames consecutivos; el evento de telemetría sí lo registra.
