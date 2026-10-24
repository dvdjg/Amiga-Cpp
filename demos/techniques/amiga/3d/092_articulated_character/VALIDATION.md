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
| Cadencia silhouette release | `measure-fps.mjs` | **ABIERTA: 12,9 fps / 3,87 campos** |
| A/B relleno por cara (referencia) | `K092_FACE=1`, release | **8,30 fps / 855.150 ciclos** |
| Aceleración de silueta | 12,9 vs 8,30 fps | **1,55x frente a cara** |
| Cadencia wire release (ablacion) | `K092_WIRE=1` | **ABIERTA: 13,3 fps debug** |

Comandos:

```bash
bash ./tools/build/build-demo.sh demos/techniques/amiga/3d/092_articulated_character --release
bash ./tools/run/run-demo.sh demos/techniques/amiga/3d/092_articulated_character --config A500_release --settle-ms 700 --sequence-frames 4 --sequence-interval-ms 400
node ./tools/analyze/verify-3d-sequence.mjs out/run/092_articulated_character/A500_release/sequence --min-frames 3 --require-motion
node ./tools/debug/measure-fps.mjs 092_articulated_character A500_release --json
```

Limite: el solver no resuelve contactos angulares ni motores de joint; la inercia se usa para impulsos/velocidad angular, no para un solver de cuerpo rigido completo. P8 sigue abierto: 090 y 092 no alcanzan 25 fps release.
## 7. Límite de fill/clear (2026-10-24)

Se probaron tres optimizaciones de relleno: clear por cada parte (6 blits), clear único más pequeño (160×156) y un solo plano de color. Resultados release: **12,75 fps** con clear por parte, **12,78** con clear pequeño y **12,91** con un plano, frente a **12,93** con el clear de 208×156 y 2 planos. Ninguna mejora: el coste restante no es el clear. Sin física se llega a **15,36 fps**, así que el relleno/clear por sí solo ya está por debajo de 25. La conclusión es que 092 es un banco de pruebas del solver y de la silueta; para 25 fps de juego habría que reducir el número/tamaño de partes o usar sprites, no seguir tocando el clear.