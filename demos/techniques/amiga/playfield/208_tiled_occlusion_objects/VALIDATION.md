# VALIDATION - 208_tiled_occlusion_objects

**Estado: VERIFICADA con excepción de rendimiento.** Composición, oclusión, ausencia de estelas y flicker comprobados. El frame ocupa 2 campos (24,92 fps), por debajo del objetivo general de 50 fps pero en el suelo de 25 fps para esta escena EHB de 6 planos con props y save-under.

## Evidencia host (26/10/2026)

- `node tools/tiled/test-cook-plan.mjs` → `OK: cook-plan contract` (reglas por `id`/`gid`/`class`, `sort_y`, `occluder`).
- `node tools/tiled/test-cook.mjs` → 5/5 OK; los objetos runtime emiten `objects.h`, `object_sheet.bin`, `object_masks.bin` y `object_pair.bin`.
- `node tools/tiled/test-parse-tmx.mjs` → 17/17 OK.
- Cocción EHB de `Beginning Fields` con `beginning-fields.objects.cook-plan.json`: 1 foreground + 1 occluder, 2 imágenes; `object_sheet.bin` 6.756 B, `object_masks.bin` 1.126 B, `object_pair.bin` 21.024 B.
- `objects.json`: id 43 `{gid:514, x:226, y:27, w:64, h:63, foot_y:90}` foreground; id 143 `{gid:490, x:224.5, y:60.25, w:46, h:62, foot_y:122.25}` occluder.

## Evidencia Amiga

- Build release: OK (`208_tiled_occlusion_objects.A500_release.exe`).
- `run-demo`: READY por canal lateral, sin `FAILED`; secuencia de 40 frames cada 250 ms en `out/run/208_tiled_occlusion_objects/A500_release/sequence/`.
- Props por hoja par interleaved (`Sprite` + `BobMaskPack::InterleavedPair`): árbol id 43 y farola id 143 visibles en su posición y con la máscara correcta.
- Actor móvil por hoja par (un blit) y save-under por Blitter sobre el anillo; el explorador de guardia se dibuja una vez en el init. No hay estelas ni restos de posiciones anteriores en la secuencia.

## Visión

- `src/VISION.md` (prompt canónico, 5 frames: f000, f010, f020, f029, f039): escena coherente, personaje con contorno limpio, fondo intacto, sin halos, recortes ni restos; el guardia permanece fijo.
- Pasada de depuración sobre recortes (`out/tmp/208-tree2`, frames 0/10/20/30/39): el actor móvil queda **oculto tras el árbol** en f000, **delante del árbol** en f010 y **oculto tras la farola** en f020; sin huecos rectangulares, estelas ni artefactos.
- La respuesta cruda de ambas pasadas está en `208_tiled_occlusion_objects_report.md`; el resumen canónico, en `src/VISION.md`.

## Rendimiento

- `node tools/debug/measure-fps.mjs 208_tiled_occlusion_objects A500_release --json` → **24,98 fps**, **284.029 ciclos/frame**, **2,002 campos/frame**, cuantización estable a 2 campos.
- Medida reproducible (8 ventanas entre 24,89 y 25,16 fps). Es una **excepción documentada**: 6 planos EHB, dos props cocidos y save-under del actor móvil; no se alcanza 50 fps y se acepta el suelo de 25 fps.
- Flicker: `tools/test-regression.sh --flicker --require-flicker-ok --release-build --demo demos/techniques/amiga/playfield/208_tiled_occlusion_objects` → **Regression OK**, sin candidatos (`flicker-baseline.json` con `max_candidates: 0`, `max_blocks_low: 0`).
- Cadencia: cuantización estable a 2 campos, sin picos entre ventanas; no se observan tirones en la secuencia.
