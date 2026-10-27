# VALIDATION - 208_tiled_occlusion_objects

**Estado: funcional con suelo de 25 fps.** Composición, oclusión del árbol, la farola y el cartel, y ausencia de estelas comprobadas. El frame ocupa **2 campos (25,0 fps)**, en el suelo de 25 fps. La demo queda verificada en rendimiento para esta escena EHB.

## Evidencia host (26/10/2026)

- `node tools/tiled/test-cook-plan.mjs` → `OK: cook-plan contract` (reglas por `id`/`gid`/`class`, `sort_y`, `occluder`).
- `node tools/tiled/test-cook.mjs` → 5/5 OK; los objetos runtime emiten `objects.h`, `object_sheet.bin`, `object_masks.bin` y `object_pair.bin`.
- `node tools/tiled/test-parse-tmx.mjs` → 17/17 OK.
- Cocción EHB de `Beginning Fields` con `beginning-fields.objects.cook-plan.json`: 1 foreground + 2 occluders, 3 imágenes; `object_sheet.bin` 8.772 B, `object_masks.bin` 1.462 B, `object_pair.bin` 27.072 B.
- `objects.json`: id 43 `{gid:514, x:226, y:27, w:64, h:63, foot_y:90}` foreground; id 143 `{gid:490, x:224.5, y:60.25, w:46, h:62, foot_y:122.25}` occluder; id 89 `{gid:486, x:246, y:161, w:48, h:42, foot_y:203}` occluder (cartel).

## Evidencia Amiga

- Build release: OK (`208_tiled_occlusion_objects.A500_release.exe`).
- `run-demo`: READY por canal lateral, sin `FAILED`; cámara `X=200`, `Y=50`.
- Props por hoja par interleaved (`Sprite` + `BobMaskPack::InterleavedPair`): árbol id 43, farola id 143 y cartel id 89 visibles con la máscara correcta.
- Actor móvil por hoja par (un blit) y save-under por Blitter sobre el anillo; el explorador de guardia se dibuja una vez en el init. No hay estelas ni restos.
- Secuencia de 8 frames cada 500 ms: el actor aparece a la izquierda del cartel, se oculta parcialmente al entrar en su rectángulo y desaparece del todo dentro de él; el cartel se pinta encima. Sin errores de recorte.

## Visión

- Prompt canónico en `src/VISION.md`; pasada de depuración sobre el cruce (`out/tmp/208_cross_vision2`, frames 0/2/4/7): el modelo describe al explorador visible antes del cartel, parcialmente oculto en f0002 y totalmente oculto en f0004; **no hay errores de recorte**. Respuesta cruda en `208_tiled_occlusion_objects_report.md`.
- En la misma pasada el modelo no ve reaparecer al actor a la derecha; la secuencia se conserva como evidencia de la ocultación y el cruce se comprueba además por el frame-diff de la zona.

## Rendimiento

- `node tools/debug/measure-fps.mjs 208_tiled_occlusion_objects A500_release --json` → **25,04 fps**, **283.284 ciclos/frame**, **1,997 campos/frame**; cuantización estable a 2 campos (24,20-26,07 fps).
- El cartel se pinta una vez en el anillo y el guardia queda horneado; el móvil se oculta por la regla de pie cuando corresponde. El coste vuelve al suelo de 25 fps.

## Flicker de alta frecuencia

- `node tools/vision-review/flicker-highfreq.mjs --demo ... --no-run --frames 12 --require-ok` → `bands=ok`, `candidates=0`, `max_blocks_low=2`.
- Informe: `out/vision-review/208_tiled_occlusion_objects/flicker-highfreq.md`.
- El test captura frames consecutivos (paso 1) para no perder una alternancia de un frame de cada dos. No captura dentro del scanout; si se sospecha tearing de buffer único, el siguiente paso es doble buffer, no relajar el umbral.
