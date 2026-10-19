# VALIDATION — 088 BLIT3D PLANES

## Alcance

Cubo convexo girando sobre 2 bitplanes, relleno por plano con Blitter (contorno ONEDOT + area fill acotado al bbox + patrón opcional), culling por winding proyectado y **fast path de frustum por objeto** (si las 8 esquinas están dentro, no se recorta ninguna cara). Se verifica: que el cubo gira con 3 caras de colores distintos y relleno limpio, y el coste por frame en debug y release. **No cubierto**: fps a 50 Hz estables, polígonos que cruzan los bordes del viewport (el clip fino de caras no se ejerce en esta escena) y solapes entre sólidos (painter, no z-buffer).

## Medidas

`node tools/debug/measure-fps.mjs 088_blit3d_planes --config <cfg> --duration 5000`.

| Config | fps | ciclos/frame | campos/frame | ventanas |
|---|---|---|---|---|
| `A500_debug` | 28.19 | 251 660 | 1.77 | 26.3–31.3 |
| `A500_release` | 30.76 | 230 602 | 1.63 | 28.1–33.7 |

Evolución de la ruta (debug): 12.5 fps (módulos por variable en el clip) → 16.7 (culling tipado sin libcalls) → 17.2 (pipeline 3D genérico sobre `Fixed`) → **28.2** (fast path de frustum; el bucle de caras baja de ~205k a ~58k ciclos/frame). Reparto actual (perfil): `actors` 33k (transform + test de frustum), `blits` 58k (clip/proyección/culling), `copper` 135k (relleno Blitter), resto 19k.

**Cadencia**: alterna frames de 1 y 2 campos (26–34 fps, dispersión ~5). No hay frames de 3+ campos, pero no se alcanza el bloqueo a 50 Hz; el presupuesto de 25 fps se cumple en todas las ventanas. El relleno (54 %) es el siguiente objetivo de coste.

**Relleno CPU (ablación)**: `PlaneFillBuilder::fill_cpu` da **1.14 fps** (6.24M ciclos/frame) en este objeto: el camino CPU no es una alternativa para polígonos pequeños; el Blitter es obligatorio aquí.

## Visión (Ollama, prompt canónico)

Resultado en `VISION.md` (4 frames): cubo con caras roja/azul/magenta, bordes limpios, sin huecos. Pregunta específica por la secuencia: «3 caras: roja frontal, azul lateral y magenta en la esquina donde se superponen, sin huecos ni artefactos». El magenta de la esquina es el **solape de arista compartida** (los dos planos toman el píxel de la arista): artefacto conocido de 1 px, no un hueco del relleno.

## Evidencia de que el clip no se ejerce

El cubo (semilado 150, centro z=900, cámara focal 340, half 160×116) está siempre dentro del frustum; por eso el fast path tapa el 100 % de los frames y el clip por cara queda **SIN VERIFICAR en hardware** en esta demo (sí está cubierto por HOST-481 con float y `Fixed<s16,0>`, y por la 089 para el relleno). Para ejercerlo: mover el cubo hacia z≈500 o reducir `half_w/half_h`.

## Comandos

```bash
bash tools/build/build-demo.sh demos/techniques/amiga/3d/088_blit3d_planes --debug
bash tools/run/run-demo.sh demos/techniques/amiga/3d/088_blit3d_planes --sequence-frames 4 --sequence-interval-ms 900
node tools/debug/measure-fps.mjs 088_blit3d_planes --config A500_debug --duration 5000
bash tools/build/build-demo.sh demos/techniques/amiga/3d/088_blit3d_planes --release
bash tools/run/run-demo.sh demos/techniques/amiga/3d/088_blit3d_planes --config A500_release --sequence-frames 1
node tools/debug/measure-fps.mjs 088_blit3d_planes --config A500_release --duration 5000
```
