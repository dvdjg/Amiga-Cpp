# EVALUACIÓN DE VISIÓN — 088_blit3d_planes

<!-- generado por tools/analyze/vision-final.mjs; no editar a mano -->

- Fecha: 2026-10-19T20:40:26.300Z
- Modelo: `qwen3-vl:8b-instruct-q8_0`
- Frames: frame_000.png, frame_001.png, frame_002.png, frame_003.png (4)
- Secuencia: `out/run/088_blit3d_planes/A500_debug/sequence`

## Prompt (canónico, tools/analyze/vision-prompt.txt)

```text
Las imágenes adjuntas van EN ORDEN TEMPORAL (si hay más de una). Describe con precisión qué ves: todos los elementos gráficos que aparecen, dónde están situados y qué hacen. Indica cualquier elemento que cambie entre imágenes y cualquier cosa que parezca extraña, defectuosa o incoherente (posiciones raras, parpadeos, recortes, artefactos, elementos superpuestos o incompletos). Presta especial atención a los objetos que se mueven: examina su CONTORNO en cada imagen (¿está limpio el perfil del objeto, sin banda o halo rectangular alrededor, sin restos de su posición anterior, sin píxeles cortados o colgando en los bordes?) y el FONDO inmediatamente alrededor (¿queda igual que cuando el objeto no estaba encima, o el objeto lo tapa con un rectángulo de color uniforme?). No adivines el propósito del programa: describe solo lo observable. Responde de forma concisa y ordenada.
```

## Respuesta (cruda)

**Imagen 1:**

*   **Objeto principal:** Un cubo 3D, compuesto por dos caras visibles. La cara frontal es un cuadrado rojo sólido. La cara lateral, visible en perspectiva, es un cuadrado azul sólido. Ambas caras están ligeramente inclinadas hacia el espectador.
*   **Posición del objeto:** El cubo está centrado en la pantalla, ligeramente desplazado hacia la derecha y hacia abajo.
*   **Fondo:** Negro uniforme.
*   **Texto en la parte inferior:**
    *   A la izquierda: "088 BLIT3D PLANES" en letras mayúsculas de color magenta, seguido de "v1.0.5390" en la línea debajo.
    *   A la derecha: "F97" en letras mayúsculas de color magenta.
*   **Estado del objeto:** El contorno del cubo es limpio, sin halos ni artefactos. El fondo negro está completamente visible alrededor del objeto, sin restos de su posición anterior.

**Imagen 2:**

*   **Objeto principal:** El mismo cubo 3D, pero ahora rotado. La cara roja sigue siendo la frontal, pero la cara azul lateral ahora está más inclinada hacia la derecha y hacia arriba, mostrando una esquina superior derecha más pronunciada.
*   **Posición del objeto:** El cubo está centrado en la pantalla, ligeramente desplazado hacia la izquierda y hacia abajo.
*   **Fondo:** Negro uniforme.
*   **Texto en la parte inferior:**
    *   A la izquierda: "088 BLIT3D PLANES" y "v1.0.5390" (idénticos a la imagen 1).
    *   A la derecha: "F79" (cambia de "F97" en la imagen 1).
*   **Estado del objeto:** El contorno del cubo es limpio, sin halos ni artefactos. El fondo negro está completamente visible alrededor del objeto, sin restos de su posición anterior.

**Cambios entre imágenes:**

*   **Rotación del cubo:** El cubo ha girado ligeramente en el eje horizontal (izquierda/derecha) y vertical (arriba/abajo), cambiando la orientación de sus caras visibles.
*   **Cambio en el texto:** El texto "F97" en la imagen 1 cambia a "F79" en la imagen 2. El resto del texto permanece igual.
