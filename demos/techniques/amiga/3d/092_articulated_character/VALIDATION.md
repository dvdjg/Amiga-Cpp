# Validacion de la demo 092

Estado: **FUNCIONAL, VISION OK; cadencia abierta**. Tres partes con masa/pivotes/esferas, joints PBD y render por silueta con Blitter (4/2 planos). La cadena se asienta y queda estatica; el objetivo de esta pasada es validar la pose articulada y el pipeline Blitter, no un ciclo de animacion continuo.

| Elemento | Evidencia | Veredicto |
|---|---|---|
| Cadena de 3 partes, masa y joints | HOST-488 (`articulated joint conserva pivote`) | **OK host** |
| Esferas por parte y suelo | HOST-488 (`articulated suelo`) | **OK host** |
| Render por silueta con Blitter (sin CPU) | Captura + `PolygonFillService` | **OK** |
| Vision de secuencia | `src/VISION.md` (Ollama, 4 frames) | **OK: contornos limpios, sin artefactos** |
| Continuidad de secuencia | `node tools/analyze/verify-3d-sequence.mjs out/run/092_articulated_character/A500_debug/sequence --min-frames 4` | **OK** |
| Cadencia | `measure-fps.mjs` | **13,60 fps / 3,68 campos (521.902 ciclos)** |

Comandos:

```bash
bash ./tools/build/build-demo.sh demos/techniques/amiga/3d/092_articulated_character --debug
bash ./tools/run/run-demo.sh demos/techniques/amiga/3d/092_articulated_character --settle-ms 500 --sequence-frames 4 --sequence-interval-ms 250
node ./tools/analyze/verify-3d-sequence.mjs out/run/092_articulated_character/A500_debug/sequence --min-frames 4
node ./tools/debug/measure-fps.mjs 092_articulated_character A500_debug --json
```

Limite: el solver actual es PBD de traslacion (joints de bola por pivotes); no integra todavia velocidad/inercia angular por parte. La inercia se calcula y almacena por parte, y el asset v2 la describe.
