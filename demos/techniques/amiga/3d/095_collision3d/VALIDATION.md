# Validacion de la demo 095

Estado: **FUNCIONAL, VISION OK**. Trigger OBB + capa/mascara + consulta esfera-OBB sobre siluetas Blitter. El jugador cruza la puerta y el HUD cambia IN/OUT; la puerta pasa de cian a rosa al entrar.

| Elemento | Evidencia | Veredicto |
|---|---|---|
| Trigger OBB y filtro de capas | `trigger_sphere` + HUD IN/OUT | **OK on-target** |
| Capas/mascaras | HOST-489 (`capas compatibles/rechazadas`, trigger) | **OK host** |
| Render Blitter | Captura release + `PolygonFillService` | **OK** |
| Movimiento y contornos | secuencia de 6 frames release | **OK: 23.256 -> 26.712 area** |
| Vision | `src/VISION.md` | **OK: cubo amarillo cruza la puerta, contornos limpios** |
| Cadencia release | `measure-fps.mjs` | **16,31 fps / 3,06 campos (434.834 ciclos)** |

La cadencia no alcanza 25 fps: el clear y los dos rellenos de silueta dominan. La demo es una validacion funcional de G6, no una demo de cadencia.

```bash
bash ./tools/build/build-demo.sh demos/techniques/amiga/3d/095_collision3d --release
bash ./tools/run/run-demo.sh demos/techniques/amiga/3d/095_collision3d --config A500_release --settle-ms 500 --sequence-frames 6 --sequence-interval-ms 400
node ./tools/analyze/verify-3d-sequence.mjs out/run/095_collision3d/A500_release/sequence --min-frames 4 --require-motion
```