# Validacion de la demo 092

Estado: **FUNCIONAL, VISION OK, cadencia abierta**. Tres partes con masa, pivotes, esferas, inercia diagonal real y estado angular; joints de bola PBD y bisagras con limites. La cadera y la pierna oscilan; el torso permanece como raiz estatica. Render por silueta con Blitter.

| Elemento | Evidencia | Veredicto |
|---|---|---|
| Cadena de 3 partes, masa y joints | HOST-488 | **OK host** |
| Inercia diagonal con termino propio | HOST-488 (`inercia incluye termino propio`) | **OK host** |
| Impulso descentrado -> par | HOST-488 (`impulso descentrado -> par`) | **OK host** |
| Bisagra con limites | HOST-488 (`bisagra limita eje...`) | **OK host** |
| Animacion angular de 092 | Secuencia de 4 frames + `src/VISION.md` | **OK vision; movimiento** |
| Render por silueta con Blitter | Captura + `PolygonFillService` | **OK** |
| Continuidad de secuencia | `verify-3d-sequence ... --require-motion` | **OK: 4 frames, movimiento** |
| Cadencia silhouette release | `measure-fps.mjs` | **ABIERTA: 13,1 fps / 3,82 campos** |
| Cadencia wire release (ablacion) | `K092_WIRE=1` | **ABIERTA: 13,3 fps debug** |

Comandos:

```bash
bash ./tools/build/build-demo.sh demos/techniques/amiga/3d/092_articulated_character --release
bash ./tools/run/run-demo.sh demos/techniques/amiga/3d/092_articulated_character --config A500_release --settle-ms 700 --sequence-frames 4 --sequence-interval-ms 400
node ./tools/analyze/verify-3d-sequence.mjs out/run/092_articulated_character/A500_release/sequence --min-frames 3 --require-motion
node ./tools/debug/measure-fps.mjs 092_articulated_character A500_release --json
```

Limite: el solver no resuelve contactos angulares ni motores de joint; la inercia se usa para impulsos/velocidad angular, no para un solver de cuerpo rigido completo. P8 sigue abierto: 090 y 092 no alcanzan 25 fps release.