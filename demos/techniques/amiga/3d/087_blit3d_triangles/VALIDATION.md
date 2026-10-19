# VALIDATION — 087_blit3d_triangles (pila 3D por Blitter, paso 1)

## Qué se ha verificado y con qué evidencia

| Check | Evidencia | Resultado |
|---|---|---|
| Núcleo del renderer (clip de cámara, proyección acotada, culling por área, pentágonos n-gon) | `tests/host/graphics/481_render3d_clip` | **HOST-481 OK** |
| Composición/display y caras sólidas | Secuencias de `run-demo --sequence-frames` (`out/run/087_blit3d_triangles/A500_debug/sequence`): triángulos amarillo/rojo/verde/cian/magenta rellenos, bordes limpios, solapes con painter correcto | **OK visual** |
| Contador de frames y versión | La franja muestra `F<num>` avanzando y `v1.0.<build>` (el contador se pinta con `DiffText`) | **OK** |
| flicker | No declarado todavía (demo abierta por presupuesto); el relleno por spans no tiene estado de paridad ni doble buffer | **Pendiente** |
| Presupuesto | `measure-fps` (8 ventanas de 2,5 s): 7,98 fps, 889 453 ciclos/frame (6,27 campos) con 7 triángulos; ventanas estables 6,3-7,5 fps | **ABIERTA** (suelo 25 fps de efectos de relleno poligonal) |

## Localización del coste (perfilador por secciones, `g_eng_prof`)

Medido con `node tools/debug/profile.mjs 087_blit3d_triangles A500_debug` (secciones 0..11, ~30-56 frames):

- **Clear del área de juego** (`blitter_clear`, 4 planos × 232 filas): ~**110k ciclos/frame** (0,78 campos).
- **Sección de caras** (7 × `render_solid` + relleno): ~**1,05M ciclos/frame**; `render_solid` completo ~**141k por cara**, de los que el **relleno por cara** son ~**83k**.
- Dentro del relleno: las **4 copias de plano (cookie-cut)** consumen ~**60k de tiempo de Blitter por cara**; los spans CPU y la programación de blits son baratos (~7k).
- El cálculo del renderer (transform + clip + proyección + área) es una fracción pequeña del total; las ablaciones confirman además que el clip y la proyección no son el problema.

## Defectos encontrados y corregidos

1. **Rayas horizontales en el relleno por cara** (contorno ONEDOT + area fill inclusivo): las filas con **un solo cruce** (aristas horizontales y vértices extremos) se rellenaban hasta el borde del bbox. Sustituido por **spans CPU en la máscara** (mismo generador convexo que la referencia) + cookie-cut por plano: sin paridad, sin rayas. Afectaba también a la ruta de triángulos del backend.
2. **Carrera clear/spans**: el clear de la máscara era asíncrono y las escrituras CPU de los spans se ejecutaban antes de que el Blitter terminara; se añadió `wait_blitter()` tras el clear (síntoma: caras invisibles intermitentes).
3. **`convex_spans` con `% n` por fila**: en 68000 el módulo de 32 bits es `__umodsi3` (~150 ciclos); sustituido por envoltura incremental `next/prev` (optimización de la referencia CPU, aplica también al relleno CPU).
4. **El perfilador leía binarios viejos**: `tools/debug/profile.mjs` no hacía staging (`dh1:a.exe`); ahora copia el ejecutable compilado antes de lanzar (mismo contrato que `measure-fps`), con lo que las medidas por sección son del binario actual.
5. **Escena sin display** (durante el desarrollo): `compose` sin la etapa `display` dejaba la escena sin lista de Copper y la pantalla mostraba el azul del Kickstart; se añadió `scene::display(res)`.

## Alcance y lo NO cubierto

- **Objetivo de 25 fps**: NO cumplido (7,98 fps). La demo queda **ABIERTA** hasta la campaña P5 del roadmap (reducir el tiempo de Blitter por cara: batch por plano para geometría disjunta, copias con menos sincronía o bajar área/nº de caras). No se relaja el umbral.
- **Winding**: el culling por área exige orden CCW en pantalla; los vértices de esta demo se emiten en ese orden (documentado en `render3d.hpp`). Una malla con winding invertido se culla salvo `double_sided`.
- **Solapes de objetos que se INTERSECAN** en 3D: painter sin z-buffer, orden incorrecto por definición (limitación documentada).
- **Subpíxel**: sin él (aristas con escalón de 1 px); fase condicional del roadmap.
- **Flicker**: sin declarar en esta fase (demo abierta); el pipeline no usa doble buffer.
