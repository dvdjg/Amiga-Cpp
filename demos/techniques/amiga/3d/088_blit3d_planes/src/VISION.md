# EVALUACIÓN DE VISIÓN — 088_blit3d_planes

<!-- generado por tools/analyze/vision-final.mjs; no editar a mano -->

- Fecha: 2026-10-19T18:30:52.593Z
- Modelo: `qwen3-vl:8b-instruct-q8_0`
- Frames: frame_000.png, frame_001.png, frame_002.png (3)
- Secuencia: `out/run/088_blit3d_planes/A500_debug/sequence`

## Prompt (canónico, tools/analyze/vision-prompt.txt)

```text
Las imágenes adjuntas van EN ORDEN TEMPORAL (si hay más de una). Describe con precisión qué ves: todos los elementos gráficos que aparecen, dónde están situados y qué hacen. Indica cualquier elemento que cambie entre imágenes y cualquier cosa que parezca extraña, defectuosa o incoherente (posiciones raras, parpadeos, recortes, artefactos, elementos superpuestos o incompletos). Presta especial atención a los objetos que se mueven: examina su CONTORNO en cada imagen (¿está limpio el perfil del objeto, sin banda o halo rectangular alrededor, sin restos de su posición anterior, sin píxeles cortados o colgando en los bordes?) y el FONDO inmediatamente alrededor (¿queda igual que cuando el objeto no estaba encima, o el objeto lo tapa con un rectángulo de color uniforme?). No adivines el propósito del programa: describe solo lo observable. Responde de forma concisa y ordenada.
```

## Respuesta (cruda)

**Imagen 1:**

*   **Objeto central:** Un cubo tridimensional, visto en perspectiva isométrica. Tiene tres caras visibles:
    *   La cara superior es roja.
    *   La cara frontal (más cercana al espectador) es magenta.
    *   La cara lateral izquierda es azul.
*   **Texto en la parte inferior izquierda:**
    *   `088 BLIT3D PLANES`
    *   `v1.0.5302`
*   **Texto en la parte inferior derecha:** `F20`
*   **Fondo:** Negro sólido.
*   **Estado del objeto:** El cubo está completamente visible, sin artefactos visibles en sus bordes.

**Imagen 2:**

*   **Objeto central:** El mismo cubo tridimensional, pero con una transformación:
    *   La cara frontal (que era magenta en la imagen 1) ahora es azul.
    *   La cara lateral izquierda (que era azul en la imagen 1) ahora es magenta.
    *   La cara superior sigue siendo roja.
*   **Texto en la parte inferior izquierda:** Igual que en la imagen 1.
*   **Texto en la parte inferior derecha:** `F121` (cambia de `F20`).
*   **Fondo:** Negro sólido.
*   **Estado del objeto:** El cubo está completamente visible, sin artefactos visibles en sus bordes.

**Cambios entre imágenes:**

*   **Color de las caras del cubo:** Se ha producido una rotación o cambio de vista que intercambia los colores de las caras frontal y lateral izquierda.
*   **Texto de estado:** El texto `F20` cambia a `F121`, lo que sugiere que el programa ha avanzado a una nueva fase o estado.
