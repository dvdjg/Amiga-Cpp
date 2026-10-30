# VALIDATION — Demo 201 (`201_ehb_map`)

## HUD de host

El HUD de depuración vive en el overlay del host (`HostHud`): nombre + número + versión, frame y fps, con telemetría de fase/blits/copper en la línea de stats. La franja inferior de 48 px se conserva negra para mantener la geometría del corkscrew. El lienzo lo limpia el engine (`XlimitedScene::begin`); la demo **no** limpia Chip RAM a mano.

## Medición vigente (debug, 2026-10)

| Medida | Comando | Resultado |
|---|---|---|
| fps | `node tools/debug/measure-fps.mjs 201_ehb_map A500_debug --json` | **24,22 fps**, **2,065 campos/frame**, 292922 ciclos/frame |
| Estabilidad | misma pasada | min 22,19 / max 24,96 fps; **cuantización a 2 campos/frame** |
| Visión (Ollama) | secuencia de 2-3 frames | mapa EHB con scroll, franja inferior negra limpia, sin basura ni texto de depuración |

La demo es un caso de **2 campos/frame** por coste del campo de 6 planos (el HUD ya no añade trabajo en viewport). No se declara 50 fps; el techo actual es ~25 fps estables.

## Revalidaci?n 2026-10-30

- `A500_release`, commit `b2d19e27`: **33,03 fps**, 214.746 ciclos/frame, 1,514 campos/frame, ventanas 27,34?43,98. Cadencia inestable; no es 50 Hz.
- La franja inferior de 48 px **no sale negra**: muestra una banda de ruido/p?xeles de color (marr?n/rosa) que cambia entre frames. Tambi?n ocurre en `A500_debug`, as? que no es un fallo de la compilaci?n release.
- El mapa EHB superior se mueve correctamente, pero el defecto inferior y la cadencia impiden dar la demo por limpia. No lo introducen `PlaneBobLayer` ni los cambios de BOBs; queda como deuda de la demo 201.
- Evidencia: `out/run/201_ehb_map/A500_release/screenshot.png` y `out/tmp/201_montage.png`.

## Reproducir

```bash
bash ./tools/build/build-demo.sh demos/techniques/amiga/playfield/201_ehb_map --debug
node tools/debug/measure-fps.mjs 201_ehb_map A500_debug --json
bash ./tools/run/run-demo.sh demos/techniques/amiga/playfield/201_ehb_map --sequence-frames 3 --sequence-interval-ms 500 --warp
```
