# Validacion de la demo 096

Estado: **FUNCIONAL, VISION OK; objetivo 25 fps alcanzado en release**. La escena tiene 8 props estáticos (una vez por buffer) y 2 naves dinámicas. La silueta se comparó contra el relleno por cara con la misma escena.

| Elemento | Evidencia | Veredicto |
|---|---|---|
| Ruta de silueta, 2 props dinámicos | `measure-fps` release | **26,93 fps / 1,86 campos (263.407 ciclos)** |
| Peor muestra de la ventana | `measure-fps` | **25,67 fps** |
| Ruta por cara (`K096_FACE=1`) | `measure-fps` release | **17,69 fps / 2,83 campos (400.973 ciclos)** |
| Ventaja de silueta | 26,93 vs 17,69 | **1,52x** |
| Props estáticos | 8 siluetas dibujadas en init | **sin coste por frame** |
| Contornos/movimiento | secuencia de 4 frames + `src/VISION.md` | **OK: nave azul en movimiento, rocas estáticas, sin artefactos** |
| Gate de secuencia | `verify-3d-sequence --require-motion` | **OK** |

La cadencia es estable en 2 campos la mayor parte de la ventana, con una muestra en 25,67 por el borde de campo; el objetivo de 25 fps se cumple pero con poco margen. La variante por cara queda muy por debajo, que es justo lo que la demo quiere mostrar.

```bash
bash ./tools/build/build-demo.sh demos/techniques/amiga/3d/096_silhouette_swarm --release
bash ./tools/run/run-demo.sh demos/techniques/amiga/3d/096_silhouette_swarm --config A500_release --settle-ms 700 --sequence-frames 4 --sequence-interval-ms 500
node ./tools/debug/measure-fps.mjs 096_silhouette_swarm A500_release --json
node ./tools/analyze/verify-3d-sequence.mjs out/run/096_silhouette_swarm/A500_release/sequence --min-frames 4 --require-motion
```