# EVALUACIÓN DE VISIÓN — 088_blit3d_planes

<!-- generado por tools/analyze/vision-final.mjs; no editar a mano -->

- Fecha: 2026-10-19T20:18:25.544Z
- Modelo: `qwen3-vl:8b-instruct-q8_0`
- Frames: frame_000.png, frame_001.png, frame_002.png (3)
- Secuencia: `out/run/088_blit3d_planes/A500_debug/sequence`

## Prompt (canónico, tools/analyze/vision-prompt.txt)

```text
Las imágenes adjuntas van EN ORDEN TEMPORAL (si hay más de una). Describe con precisión qué ves: todos los elementos gráficos que aparecen, dónde están situados y qué hacen. Indica cualquier elemento que cambie entre imágenes y cualquier cosa que parezca extraña, defectuosa o incoherente (posiciones raras, parpadeos, recortes, artefactos, elementos superpuestos o incompletos). Presta especial atención a los objetos que se mueven: examina su CONTORNO en cada imagen (¿está limpio el perfil del objeto, sin banda o halo rectangular alrededor, sin restos de su posición anterior, sin píxeles cortados o colgando en los bordes?) y el FONDO inmediatamente alrededor (¿queda igual que cuando el objeto no estaba encima, o el objeto lo tapa con un rectángulo de color uniforme?). No adivines el propósito del programa: describe solo lo observable. Responde de forma concisa y ordenada.
```

## Respuesta (cruda)

**Imagen 1:**

*   **Objeto principal:** Un cubo 3D, compuesto por dos caras visibles. La cara superior es de color rojo sólido. La cara frontal (o lateral) es de color azul sólido. El borde donde se encuentran ambas caras es visible y tiene un tono morado/púrpura.
*   **Posición y orientación:** El cubo está centrado en la parte superior derecha de la pantalla, ligeramente inclinado hacia la derecha y hacia abajo.
*   **Fondo:** Negro uniforme.
*   **Elementos de interfaz:**
    *   A la izquierda, una línea vertical delgada de color rojo.
    *   En la esquina inferior izquierda, texto en color magenta: "088 BLIT3D PLANES" y debajo "v1.0.5382".
    *   En la esquina inferior derecha, texto en color magenta: "F26".
*   **Contorno del objeto:** El contorno del cubo es limpio, sin halos ni artefactos visibles. Los bordes están bien definidos.
*   **Fondo alrededor del objeto:** El fondo negro permanece uniforme y no muestra artefactos ni restos de la posición anterior del objeto.

**Imagen 2:**

*   **Objeto principal:** El mismo cubo 3D, pero ahora solo se ve una cara visible: la cara superior, de color rojo sólido. La cara azul que estaba visible en la imagen anterior ha desaparecido.
*   **Posición y orientación:** El cubo está en una posición ligeramente diferente, más inclinado hacia la derecha y hacia abajo que en la imagen anterior.
*   **Fondo:** Negro uniforme.
*   **Elementos de interfaz:**
    *   A la izquierda, la línea vertical roja permanece en la misma posición.
    *   En la esquina inferior izquierda, el texto "088 BLIT3D PLANES" y "v1.0.5382" permanece igual.
    *   En la esquina inferior derecha, el texto ha cambiado de "F26" a "F91".
*   **Contorno del objeto:** El contorno del cubo es limpio, sin halos ni artefactos visibles. Los bordes están bien definidos.
*   **Fondo alrededor del objeto:** El fondo negro permanece uniforme y no muestra artefactos ni restos de la posición anterior del objeto.

**Cambios entre imágenes:**

*   La cara azul del cubo desaparece en la segunda imagen.
*   La posición del cubo cambia ligeramente.
*   El texto en la esquina inferior derecha cambia de "F26" a "F91".
