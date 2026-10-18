# EVALUACIÓN DE VISIÓN — 085_fill_clip_probe

<!-- generado por tools/analyze/vision-final.mjs; no editar a mano -->

- Fecha: 2026-10-18T19:26:06.373Z
- Modelo: `qwen3-vl:8b-instruct-q8_0`
- Frames: frame_000.png, frame_002.png, frame_004.png, frame_005.png, frame_007.png (5)
- Secuencia: `out/run/085_fill_clip_probe/A500_debug/sequence`

## Prompt (canónico, tools/analyze/vision-prompt.txt)

```text
Las imágenes adjuntas van EN ORDEN TEMPORAL (si hay más de una). Describe con precisión qué ves: todos los elementos gráficos que aparecen, dónde están situados y qué hacen. Indica cualquier elemento que cambie entre imágenes y cualquier cosa que parezca extraña, defectuosa o incoherente (posiciones raras, parpadeos, recortes, artefactos, elementos superpuestos o incompletos). Presta especial atención a los objetos que se mueven: examina su CONTORNO en cada imagen (¿está limpio el perfil del objeto, sin banda o halo rectangular alrededor, sin restos de su posición anterior, sin píxeles cortados o colgando en los bordes?) y el FONDO inmediatamente alrededor (¿queda igual que cuando el objeto no estaba encima, o el objeto lo tapa con un rectángulo de color uniforme?). No adivines el propósito del programa: describe solo lo observable. Responde de forma concisa y ordenada.
```

## Respuesta (cruda)

**Imagen 1:**
- **Fondo:** Un fondo de color azul marino oscuro y uniforme.
- **Elementos gráficos:**
  - Un **cuadrado blanco** situado en la parte superior izquierda. Su borde inferior está parcialmente recortado por el borde inferior de la imagen.
  - Un **triángulo rojo** situado en el centro de la imagen, orientado con su vértice inferior hacia abajo y su base en la parte superior. Su borde superior está parcialmente recortado por el borde superior de la imagen.
- **Estado:** Los elementos están fijos, sin movimiento ni artefactos visibles.

**Imagen 2:**
- **Fondo:** El fondo es azul marino oscuro, pero ahora está cubierto por una **superposición de líneas verticales negras** que se extienden desde la parte superior hasta la inferior de la imagen, creando un patrón de rayas.
- **Elementos gráficos:**
  - El **cuadrado blanco** de la imagen 1 ha sido reemplazado por un **cuadrado de color rosa pálido** en la misma posición superior izquierda. Su borde inferior también está recortado.
  - El **triángulo rojo** permanece en la misma posición y forma que en la imagen 1.
  - Aparece un **pequeño elemento verde** en la parte inferior izquierda, cerca del borde inferior de la imagen. Tiene forma de una pequeña montaña o una mancha irregular.
- **Estado:** Los elementos están fijos, pero el fondo ha cambiado y se ha añadido un nuevo elemento.

**Imagen 3:**
- **Fondo:** El fondo es azul marino oscuro, pero está completamente cubierto por un **patrón de ruido blanco y negro** (como una imagen con interferencia de televisión o ruido digital).
- **Elementos gráficos:**
  - El **cuadrado blanco** de la imagen 1 ha sido reemplazado por un **cuadrado con un patrón de ruido blanco y negro**, que se integra con el fondo ruidoso. Su borde inferior está recortado.
  - El **triángulo rojo** permanece en la misma posición y forma que en las imágenes anteriores, pero su contorno está borroso y se mezcla con el ruido del fondo.
- **Estado:** Los elementos están fijos, pero el fondo y los elementos gráficos están completamente afectados por el ruido.

**Cambios entre imágenes:**
- El fondo cambia de uniforme a rayas verticales y luego a ruido.
- El cuadrado blanco cambia a rosa pálido en la imagen 2 y a ruido en la imagen 3.
- Aparece un nuevo elemento verde en la imagen 2.
- El triángulo rojo permanece en la misma posición en todas las imágenes, aunque su contorno se vuelve borroso en la imagen 3.

**Elementos extraños o defectuosos:**
- En todas las imágenes, los bordes inferiores del cuadrado y del triángulo están recortados por el borde inferior de la imagen, lo que sugiere que los elementos están parcialmente fuera del área visible.
- En la imagen 3, el triángulo rojo y el cuadrado están completamente integrados en el ruido, lo que hace que su contorno no sea limpio ni definido.
