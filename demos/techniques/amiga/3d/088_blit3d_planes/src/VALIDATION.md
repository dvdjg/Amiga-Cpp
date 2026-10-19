# VALIDATION — 088 BLIT3D PLANES

## Alcance

Cubo convexo girando sobre 2 bitplanes, relleno por plano con Blitter (contorno ONEDOT + area fill acotado al bbox + patrón opcional), **culling por normal 3D general para cualquier convexo** (caras CCW desde fuera; cámara en el origen), **fast path de frustum por objeto** y **proyección única de los 8 vértices** (no por cara). Se verifica: cubo con 3 caras de colores y relleno limpio, coste debug/release, y el **clip fino en hardware** con el modo de deriva (`kDrift`). **No cubierto**: bloqueo a 50 Hz, solapes entre sólidos (painter, no z-buffer) y el artefacto de arista compartida (1 px).

## Medidas

`node tools/debug/measure-fps.mjs 088_blit3d_planes --config <cfg> --duration 5000`.

| Config | fps | ciclos/frame | campos/frame | ventanas |
|---|---|---|---|---|
| `A500_debug` | 32.56 | 217 890 | 1.54 | 30.1–35.3 |
| `A500_release` | 33.87 | 209 424 | 1.48 | 31.4–37.2 |

Evolución (debug): 12.5 fps (módulos por variable en el clip) → 16.7 (culling tipado sin libcalls) → 17.2 (pipeline 3D genérico sobre `Fixed`) → 28.2 (fast path de frustum) → **32.6** (proyección única de los 8 vértices; el bucle de caras baja a ~40k). Reparto: `actors` (transform + test de frustum) ~47k, `blits` (culling/área) ~40k, `copper` (relleno Blitter) ~130k, resto ~19k.

**Culling general**: ace_visible (mesh_traits) descarta caras traseras antes de proyectar/recortar; sirve para cualquier poliedro convexo irregular (probado en HOST-481 con un tetraedro irregular bajo 64 rotaciones: coincide con el signo del área proyectada). En el modo kDrift (cruces de borde) sube de 20.3 a **26.5 fps** (ventanas 22.6–30.9).

**Cadencia**: alterna frames de 1 y 2 campos (30–37 fps, dispersión ~4). No hay frames de 3+ campos; el suelo de 25 fps se cumple en todas las ventanas, sin bloqueo a 50 Hz. El relleno (65 %) es el coste dominante y su varianza (93k–169k) explica la alternancia.

## Relleno CPU de referencia

`fill_polygons_by_plane_patterns_cpu` se reescribió con **active edge list** (una división por arista, acumulación de x en 16.16 por fila) en lugar de dividir por cruce y por fila. En la 088 (2 planos, cubo) pasa de 1.14 fps (6.24M ciclos/frame) a **7.04 fps** (1.01M): sigue 4× por detrás del Blitter, así que la demo usa el Blitter; el camino CPU queda como referencia portable (Atari ST/Mega Drive). Host: `217_polygon_planes`, `474_fill_patterns` y `479_clip_partial` en verde (interior idéntico, recorte parcial sin pérdidas más allá de 1 px).

## Clip fino en hardware (modo `kDrift`)

Con `kDrift = true` el cubo se desplaza lateralmente y cruza el borde derecho. Secuencia de 6 frames: **recorte limpio, sin huecos, basura ni restos del frame anterior** (Ollama). Coste medido en ese modo: 20.3 fps de media con ventanas de **16.1–28.5** (el clip por cara y los rellenos parciales cuestan ~130k); por eso `kDrift` queda **off por defecto** y la demo entrega 32.6 fps. El modo es la evidencia de clip en hardware; activarlo para reproducirla.

## Visión (Ollama, prompt canónico)

Resultado en `VISION.md` (4 frames): cubo con caras roja/azul/magenta, bordes limpios, sin huecos. El magenta de la esquina es el **solape de arista compartida** (los dos planos toman el píxel de la arista): artefacto conocido de 1 px, no un hueco del relleno.

## Comandos

```bash
bash tools/build/build-demo.sh demos/techniques/amiga/3d/088_blit3d_planes --debug
bash tools/run/run-demo.sh demos/techniques/amiga/3d/088_blit3d_planes --sequence-frames 4 --sequence-interval-ms 900
node tools/debug/measure-fps.mjs 088_blit3d_planes --config A500_debug --duration 5000
bash tools/build/build-demo.sh demos/techniques/amiga/3d/088_blit3d_planes --release
bash tools/run/run-demo.sh demos/techniques/amiga/3d/088_blit3d_planes --config A500_release --sequence-frames 1
node tools/debug/measure-fps.mjs 088_blit3d_planes --config A500_release --duration 5000
```
