# Validación — 099 Star Fox travel (Corneria, todo 3D)

## Estado

Escena Corneria con **objetos 3D** (Arwing, dos enemigos, arco y edificio) y fondo de cielo/terreno/agua con **tres variantes pre-rotadas** (-6°, 0°, +6°). Cámara con yaw/pitch/roll y trayectoria zigzagueante. **3,65 fps** en release A500; no cumple >6 fps. Es la excepción aceptada para tener arquitectura 3D real.

## Comandos

```bash
export AMIGA_BIN_PATH="$USERPROFILE/Documents/programa/AI/Amiga/vscode-amiga-debug/bin/win32"
export AMIGA_WINUAE_PATH='C:\Users\dvdjg\Documents\programa\AI\Amiga\WinUAE-DBG\bin'
export WINUAE_GDB_PORT=2355
export WINUAE_SIDE_CHANNEL_PORT=2421

./tools/build/build-demo.sh demos/techniques/amiga/3d/099_starfox_travel --release
./tools/run/run-demo.sh demos/techniques/amiga/3d/099_starfox_travel --config A500_release --settle-ms 1000
node tools/debug/measure-fps.mjs 099_starfox_travel A500_release --json

./tools/run/run-demo.sh demos/techniques/amiga/3d/099_starfox_travel --config A500_release --sequence-frames 8
node tools/analyze/verify-3d-sequence.mjs out/run/099_starfox_travel/A500_release/sequence --min-frames 8 --require-motion --require-distinct
./tools/test-regression.sh --flicker --require-flicker-ok --demo demos/techniques/amiga/3d/099_starfox_travel
```

## Rendimiento

| Versión | fps | Ciclos/frame | Campos/frame | Mínimo | Estado |
|---|---:|---:|---:|---:|---|
| **Todo 3D (entrega)** | **3,65** | **1.943.341** | **13,697** | **3,1** | Aceptado por el usuario |
| Arquitectura estática (7,36 fps) | 7,36 | 963.242 | 6,789 | 7,1 | Descartada por “todo 3D” |
| Escenario 3D denso probado | ~2,3 | 3.055.504 | 21,536 | 2,0 | Descartado |

El coste dominante son las caras del arco y del edificio 3D; el fondo pre-rotado se copia con Blitter. Con dos objetos de arquitectura se queda en 3,65 fps; añadir más baja a ~2,3.

## Fondo pre-rotado y cámara

- Tres variantes de fondo de 384×208 en Chip (`-6°`, `0°`, `+6°`). Se eligen por el roll de cámara (`m_cam_roll_q12`) y se copian con `blitter_copy_words_strided`; la ventana de 320 px se desplaza en X (yaw) y en Y (pitch).
- El horizonte de cada variante se pinta como un polígono de terreno/agua inclinado; el roll no es un simple offset.
- Los objetos 3D reciben yaw/pitch/roll con `apply_camera`; el jugador añade su propia trayectoria.
- El HUD de bitplanes se pinta por buffer y no se copia; el HUD de host se refresca cada 16 frames.

## Objetos 3D

- Arwing: 23 vértices/31 triángulos (cabina elevada azul). Enemigo: 11/14. Quads triangulados; normales orientadas por el centro del modelo.
- Arco: 24 vértices, caras n-gon. Edificio: 12 vértices, caja con caras n-gon. Ambos con culling, sombreado por sol común, painter por `z` y spans CPU opacos de 4 planos.
- La vista de depuración 3/4 del Arwing se reproduce con `EXTRA_DEFINES=-DK099_DEBUG_ARWING=1`; el jugador se coloca sin enemigos en primer plano. Captura: `out/vision-review/099_arwing_3q.png`.

## Secuencia y visión

Secuencia de 8 frames (756×576) en `out/run/099_starfox_travel/A500_release/sequence/`:

```text
visual: 7/7 cambios; maxEstatico=0
areas: 270408 en los 8 frames (el fondo domina)
```

Visión canónica: [`src/VISION.md`](src/VISION.md). El modelo describe el paisaje, el bloque/edificio gris, el arco, la nave azul/blanca, el horizonte inclinado y el movimiento; contornos limpios y sin artefactos.

## Flicker y regresión

```text
Regression report: out/regression/20261025-075917/regression-report.md
099_starfox_travel | build ok | run ok | analyze ok | flicker ok
candidatos deterministas: 0
max bloques bajos: 166 <= baseline 220
```

La baseline 220 cubre el movimiento del fondo/cámara; el par grande es desplazamiento coherente, no flicker.

## Conclusión

La entrega cumple **todo 3D** (naves + arco + edificio), con roll real de fondo por variantes y cámara zigzagueante. **No cumple 6 fps**: 3,65 fps. La alternativa estática a 7,36 fps se descartó por decisión explícita del usuario. La deuda para recuperar fps sin volver a 2D es el renderer de escenario por planos con Blitter o BOBs pre-renderizados (que no serían geometría pura).
