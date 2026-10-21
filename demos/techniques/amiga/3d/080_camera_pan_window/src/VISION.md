# EVALUACIÓN DE VISIÓN — 080_camera_pan_window

<!-- generado por tools/analyze/vision-final.mjs; no editar a mano -->

- Fecha: 2026-10-21T16:13:22.599Z
- Modelo: `qwen3-vl:8b-instruct-q8_0`
- Frames: frame_000.png, frame_086.png, frame_173.png, frame_259.png (4)
- Secuencia: `out/run/080_camera_pan_window/A500_debug/sequence`

## Prompt (canónico, tools/analyze/vision-prompt.txt)

```text
Las imágenes adjuntas van EN ORDEN TEMPORAL (si hay más de una). Describe con precisión qué ves: todos los elementos gráficos que aparecen, dónde están situados y qué hacen. Indica cualquier elemento que cambie entre imágenes y cualquier cosa que parezca extraña, defectuosa o incoherente (posiciones raras, parpadeos, recortes, artefactos, elementos superpuestos o incompletos). Presta especial atención a los objetos que se mueven: examina su CONTORNO en cada imagen (¿está limpio el perfil del objeto, sin banda o halo rectangular alrededor, sin restos de su posición anterior, sin píxeles cortados o colgando en los bordes?) y el FONDO inmediatamente alrededor (¿queda igual que cuando el objeto no estaba encima, o el objeto lo tapa con un rectángulo de color uniforme?). No adivines el propósito del programa: describe solo lo observable. Responde de forma concisa y ordenada.
```

## Respuesta (cruda)

**Imagen 1:**

*   **Escena:** Vista frontal de un pasillo o corredor con paredes azules, techo verde y suelo marrón. La perspectiva es isométrica.
*   **Objetos:** En el centro, dos estructuras simétricas. Cada una consiste en una columna vertical de píxeles (azul, amarillo, naranja) y un bloque de forma irregular (uno verde, otro amarillo) que parece estar en movimiento o girando.
*   **Texto en la parte inferior:** Una barra negra con texto blanco que muestra información técnica:
    *   `080 CAMERA PAN WINDOW`
    *   `FULL 12.5 + FAKE 25`
    *   `FRAME 08FUELLO FAZEFAKE 359`
    *   `MODE YAW Y 02PP00FF149KK33`
*   **Estado de los objetos:** Los bloques centrales están en una posición específica, con sus bordes limpios y sin artefactos visibles. El fondo inmediato alrededor de ellos es uniforme y no muestra restos de posiciones anteriores.

**Imagen 2:**

*   **Escena:** La cámara se ha movido hacia la derecha, mostrando ahora una vista lateral del pasillo. La perspectiva sigue siendo isométrica.
*   **Objetos:** La estructura de la derecha está ahora más visible. El bloque amarillo y naranja se ha desplazado hacia la derecha y parece haber girado. Se ha añadido un nuevo bloque de color magenta en la parte superior derecha, que no estaba en la imagen anterior. La estructura de la izquierda está parcialmente fuera del encuadre.
*   **Texto en la parte inferior:** El texto es idéntico en formato, pero los valores han cambiado:
    *   `080 CAMERA PAN WINDOW`
    *   `FULL 12.5 + FAKE 25`
    *   `FRAME 3504FULL2800FAKE71515`
    *   `MODE PANCY 425 P 03FF197KK33`
*   **Estado de los objetos:** Los bordes de los objetos siguen siendo limpios. El fondo inmediato alrededor de los objetos es uniforme, y no hay artefactos visibles. El nuevo bloque magenta aparece con un contorno limpio, sin restos de su posición anterior.

**Cambios entre imágenes:**

*   **Movimiento de la cámara:** La cámara se ha desplazado hacia la derecha (pan).
*   **Posición de los objetos:** Los bloques centrales han cambiado de posición y orientación. Se ha añadido un nuevo bloque magenta.
*   **Texto:** Los valores en el texto de la parte inferior han cambiado, reflejando el nuevo estado de la cámara y el frame.
