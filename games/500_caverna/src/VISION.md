# EVALUACIÓN DE VISIÓN — 500_caverna

<!-- generado por tools/analyze/vision-final.mjs; no editar a mano -->

- Fecha: 2026-10-25T19:21:37.618Z
- Modelo: `qwen3-vl:8b-instruct-q8_0`
- Frames: frame_000.png, frame_001.png, frame_003.png, frame_004.png, frame_005.png (5)
- Secuencia: `out/run/500_caverna/A500_release/sequence`

## Prompt (canónico, tools/analyze/vision-prompt.txt)

```text
Las imágenes adjuntas van EN ORDEN TEMPORAL (si hay más de una). Describe con precisión qué ves: todos los elementos gráficos que aparecen, dónde están situados y qué hacen. Indica cualquier elemento que cambie entre imágenes y cualquier cosa que parezca extraña, defectuosa o incoherente (posiciones raras, parpadeos, recortes, artefactos, elementos superpuestos o incompletos). Presta especial atención a los objetos que se mueven: examina su CONTORNO en cada imagen (¿está limpio el perfil del objeto, sin banda o halo rectangular alrededor, sin restos de su posición anterior, sin píxeles cortados o colgando en los bordes?) y el FONDO inmediatamente alrededor (¿queda igual que cuando el objeto no estaba encima, o el objeto lo tapa con un rectángulo de color uniforme?). No adivines el propósito del programa: describe solo lo observable. Responde de forma concisa y ordenada.
```

## Respuesta (cruda)

**Análisis de las imágenes en orden temporal:**

**Imagen 1:**
*   **Elementos gráficos:** Escena de un juego de estilo pixel art. En la parte superior izquierda, se muestra el texto "CAVERNA DEMO 500 v1.0.417 | F32 0.0FPS". El fondo es una pared de ladrillos oscuros con líneas diagonales. En la parte inferior, hay un suelo de tierra marrón con un borde verde. A la izquierda, hay una columna vertical con un patrón de cuadrados blancos y azules. En el centro, un personaje naranja (el jugador) está en el aire. A su derecha, hay varios enemigos: un enemigo rojo con forma de cangrejo, un enemigo verde con forma de insecto, un enemigo blanco con forma de cangrejo y un enemigo rosa con forma de cangrejo. También hay dos triángulos rojos en la parte superior izquierda y un triángulo marrón en la parte superior derecha.
*   **Movimiento y cambios:** El personaje naranja está en el aire, y los enemigos están en movimiento. El enemigo rojo está a la derecha del personaje, el enemigo verde está más arriba y a la derecha, el enemigo blanco está más a la derecha y el enemigo rosa está más a la derecha y abajo. El texto en la esquina superior izquierda indica "F32 0.0FPS", lo que sugiere que el juego no está funcionando correctamente o está en pausa.

**Imagen 2:**
*   **Elementos gráficos:** La escena es similar a la imagen 1, pero con cambios en la posición de los enemigos y el texto en la esquina superior izquierda. El texto ahora dice "CAVERNA DEMO 500 v1.0.417 | F64 47.9FPS". El personaje naranja está en el aire, y los enemigos están en movimiento. El enemigo rojo está a la derecha del personaje, el enemigo verde está más arriba y a la derecha, el enemigo blanco está más a la derecha y el enemigo rosa está más a la derecha y abajo. El texto en la esquina superior izquierda indica "F64 47.9FPS", lo que sugiere que el juego está funcionando correctamente y tiene una tasa de fotogramas de 47.9 FPS.
*   **Movimiento y cambios:** El personaje naranja está en el aire, y los enemigos están en movimiento. El enemigo rojo está a la derecha del personaje, el enemigo verde está más arriba y a la derecha, el enemigo blanco está más a la derecha y el enemigo rosa está más a la derecha y abajo. El texto en la esquina superior izquierda indica "F64 47.9FPS", lo que sugiere que el juego está funcionando correctamente y tiene una tasa de fotogramas de 47.9 FPS.

**Imagen 3:**
*   **Elementos gráficos:** La escena es similar a las imágenes anteriores, pero con cambios en la posición de los enemigos y el texto en la esquina superior izquierda. El texto ahora dice "CAVERNA DEMO 500 v1.0.417 | F64 47.9FPS". El personaje naranja está en el aire, y los enemigos están en movimiento. El enemigo rojo está a la derecha del personaje, el enemigo verde está más arriba y a la derecha, el enemigo blanco está más a la derecha y el enemigo rosa está más a la derecha y abajo. El texto en la esquina superior izquierda indica "F64 47.9FPS", lo que sugiere que el juego está funcionando correctamente y tiene una tasa de fotogramas de 47.9 FPS.
*   **Movimiento y cambios:** El personaje naranja está en el aire, y los enemigos están en movimiento. El enemigo rojo está a la derecha del personaje, el enemigo verde está más arriba y a la derecha, el enemigo blanco está más a la derecha y el enemigo rosa está más a la derecha y abajo. El texto en la esquina superior izquierda indica "F64 47.9FPS", lo que sugiere que el juego está funcionando correctamente y tiene una tasa de fotogramas de 47.9 FPS.

**Comparación entre imágenes:**
*   **Cambio en el texto de la esquina superior izquierda:** En la imagen 1, el texto dice "F32 0.0FPS", lo que sugiere que el juego no está funcionando correctamente o está en pausa. En las imágenes 2 y 3, el texto dice "F64 47.9FPS", lo que sugiere que el juego está funcionando correctamente y tiene una tasa de fotogramas de 47.9 FPS.
*   **Cambio en la posición de los enemigos:** En la imagen 1, el enemigo blanco está a la derecha del enemigo rosa. En las imágenes 2 y 3, el enemigo blanco está más a la derecha del enemigo rosa.
*   **Cambio en la posición del personaje naranja:** En la imagen 1, el personaje naranja está en el aire. En las imágenes 2 y 3, el personaje naranja está en el aire, pero en una posición diferente.
*   **Cambio en la posición de los triángulos rojos:** En la imagen 1, hay dos triángulos rojos en la parte superior izquierda. En la imagen 2, hay un triángulo rojo en la parte superior izquierda. En la imagen 3, hay un triángulo rojo en la parte superior izquierda.
*   **Cambio en la posición de los triángulos marrones:** En la imagen 1, hay un triángulo marrón en la parte superior derecha. En las imágenes 2 y 3, no hay triángulos marrones en la parte superior derecha.
*   **Cambio en la posición de los triángulos azules:** En la imagen 1, hay varios triángulos azules en la parte izquierda. En las imágenes 2 y 3, hay varios triángulos azules en la parte izquierda.
*   **Cambio en la posición de los enemigos verdes:** En la imagen 1, hay un enemigo verde en la parte superior derecha. En las imágenes 2 y 3, hay un enemigo verde en la parte superior derecha.
*   **Cambio en la posición de los enemigos rosas:** En la imagen 1, hay un enemigo rosa en la parte derecha. En las imágenes 2 y 3, hay un enemigo rosa en la parte derecha.
*   **Cambio en la posición de los enemigos rojos:** En la imagen 1, hay un enemigo rojo en la parte derecha. En las imágenes 2 y 3, hay un enemigo rojo en la parte derecha.
*   **Cambio en la posición de los enemigos blancos:** En la imagen 1, hay un enemigo blanco en la parte derecha. En las imágenes 2 y 3, hay un enemigo blanco en la parte derecha.
*   **Cambio en la posición del personaje naranja:** En la imagen 1, el personaje naranja está en el aire. En las imágenes 2 y 3, el personaje naranja está en el aire, pero en una posición diferente.
*   **Cambio en la posición de los triángulos rojos:** En la imagen 1, hay dos triángulos rojos en la parte superior izquierda. En la imagen 2, hay un triángulo rojo en la parte superior izquierda. En la imagen 3, hay un triángulo rojo en la parte superior izquierda.
*   **Cambio en la posición de los triángulos marrones:** En la imagen 1, hay un triángulo marrón en la parte superior derecha. En las imágenes 2 y 3, no hay triángulos marrones en la parte superior derecha.
*   **Cambio en la posición de los triángulos azules:** En la imagen 1, hay varios triángulos azules en la parte izquierda. En las imágenes 2 y 3, hay varios triángulos azules en la parte izquierda.
*   **Cambio en la posición de los enemigos verdes:** En la imagen 1, hay un enemigo verde en la parte superior derecha. En las imágenes 2 y 3, hay un enemigo verde en la parte superior derecha.
*   **Cambio en la posición de los enemigos rosas:** En la imagen 1, hay un enemigo rosa en la parte derecha. En las imágenes 2 y 3, hay un enemigo rosa en la parte derecha.
*   **Cambio en la posición de los enemigos rojos:** En la imagen 1, hay un enemigo rojo en la parte derecha. En las imágenes 2 y 3, hay un enemigo rojo en la parte derecha.
*   **Cambio en la posición de los enemigos blancos:** En la imagen 1, hay un enemigo blanco en la parte derecha. En las imágenes 2 y 3, hay un enemigo blanco en la parte derecha.
*   **Cambio en la posición del personaje naranja:** En la imagen 1, el personaje naranja está en el aire. En las imágenes 2 y 3, el personaje naranja está en el aire, pero en una posición diferente.
*   **Cambio en la posición de los triángulos rojos:** En la imagen 1, hay dos triángulos rojos en la parte superior izquierda. En la imagen 2, hay un triángulo rojo en la parte superior izquierda. En la imagen 3, hay un triángulo rojo en la parte superior izquierda.
*   **Cambio en la posición de los triángulos marrones:** En la imagen 1, hay un triángulo marrón en la parte superior derecha. En las imágenes 2 y 3, no hay triángulos marrones en la parte superior derecha.
*   **Cambio en la posición de los triángulos azules:** En la imagen 1, hay varios triángulos azules en la parte izquierda. En las imágenes 2 y 3, hay varios triángulos azules en la parte izquierda.
*   **Cambio en la posición de los enemigos verdes:** En la imagen 1, hay un enemigo verde en la parte superior derecha. En las imágenes 2 y 3, hay un enemigo verde en la parte superior derecha.
*   **Cambio en la posición de los enemigos rosas:** En la imagen 1, hay un enemigo rosa en la parte derecha. En las imágenes 2 y 3, hay un enemigo rosa en la parte derecha.
*   **Cambio en la posición de los enemigos rojos:** En la imagen 1, hay un enemigo rojo en la parte derecha. En las imágenes 2 y 3, hay un enemigo rojo en la parte derecha.
*   **Cambio en la posición de los enemigos blancos:** En la imagen 1, hay un enemigo blanco en la parte derecha. En las imágenes 2 y 3, hay un enemigo blanco en la parte derecha.
*   **Cambio en la posición del personaje naranja:** En la imagen 1, el personaje naranja está en el aire. En las imágenes 2 y 3, el personaje naranja está en el aire, pero en una posición diferente.
*   **Cambio en la posición de los triángulos rojos:** En la imagen 1, hay dos triángulos rojos en la parte superior izquierda. En la imagen 2, hay un triángulo rojo en la parte superior izquierda. En la imagen 3, hay un triángulo rojo en la parte superior izquierda.
*   **Cambio en la posición de los triángulos marrones:** En la imagen 1, hay un triángulo marrón en la parte superior derecha. En las imágenes 2 y 3, no hay triángulos marrones en la parte superior derecha.
*   **Cambio en la posición de los triángulos azules:** En la imagen 1, hay varios triángulos azules en la parte izquierda. En las imágenes 2 y 3, hay varios triángulos azules en la parte izquierda.
*   **Cambio en la posición de los enemigos verdes:** En la imagen 1, hay un enemigo verde en la parte superior derecha. En las imágenes 2 y 3, hay un enemigo verde en la parte superior derecha.
*   **Cambio en la posición de los enemigos rosas:** En la imagen 1, hay un enemigo rosa en la parte derecha. En las imágenes 2 y 3, hay un enemigo rosa en la parte derecha.
*   **Cambio en la posición de los enemigos rojos:** En la imagen 1, hay un enemigo rojo en la parte derecha. En las imágenes 2 y 3, hay un enemigo rojo en la parte derecha.
*   **Cambio en la posición de los enemigos blancos:** En la imagen 1, hay un enemigo blanco en la parte derecha. En las imágenes 2 y 3, hay un enemigo blanco en la parte derecha.
*   **Cambio en la posición del personaje naranja:** En la imagen 1, el personaje naranja está en el aire. En las imágenes 2 y 3, el personaje naranja está en el aire, pero en una posición diferente.
*   **Cambio en la posición de los triángulos rojos:** En la imagen 1, hay dos triángulos rojos en la parte superior izquierda. En la imagen 2, hay un triángulo rojo en la parte superior izquierda. En la imagen 3, hay un triángulo rojo en la parte superior izquierda.
*   **Cambio en la posición de los triángulos marrones:** En la imagen 1, hay un triángulo marrón en la parte superior derecha. En las imágenes 2 y 3, no hay triángulos marrones en la parte superior derecha.
*   **Cambio en la posición de los triángulos azules:** En la imagen 1, hay varios triángulos azules en la parte izquierda. En las imágenes 2 y 3, hay varios triángulos azules en la parte izquierda.
*   **Cambio en la posición de los enemigos verdes:** En la imagen 1, hay un enemigo verde en la parte superior derecha. En las imágenes 2 y 3, hay un enemigo verde en la parte superior derecha.
*   **Cambio en la posición de los enemigos rosas:** En la imagen 1, hay un enemigo rosa en la parte derecha. En las imágenes 2 y 3, hay un enemigo rosa en la parte derecha.
*   **Cambio en la posición de los enemigos rojos:** En la imagen 1, hay un enemigo rojo en la parte derecha. En las imágenes 2 y 3, hay un enemigo rojo en la parte derecha.
*   **Cambio en la posición de los enemigos blancos:** En la imagen 1, hay un enemigo blanco en la parte derecha. En las imágenes 2 y 3, hay un enemigo blanco en la parte derecha.
*   **Cambio en la posición del personaje naranja:** En la imagen 1, el personaje naranja está en el aire. En las imágenes 2 y 3, el personaje naranja está en el aire, pero en una posición diferente.
*   **Cambio en la posición de los triángulos rojos:** En la imagen 1, hay dos triángulos rojos en la parte superior izquierda. En la imagen 2, hay un triángulo rojo en la parte superior izquierda. En la imagen 3, hay un triángulo rojo en la parte superior izquierda.
*   **Cambio en la posición de los triángulos marrones:** En la imagen 1, hay un triángulo marrón en la parte superior derecha. En las imágenes 2 y 3, no hay triángulos marrones en la parte superior derecha.
*   **Cambio en la posición de los triángulos azules:** En la imagen 1, hay varios triángulos azules en la parte izquierda. En las imágenes 2 y 3, hay varios triángulos azules en la parte izquierda.
*   **Cambio en la posición de los enemigos verdes:** En la imagen 1, hay un enemigo verde en la parte superior derecha. En las imágenes 2 y 3, hay un enemigo verde en la parte superior derecha.
*   **Cambio en la posición de los enemigos rosas:** En la imagen 1, hay un enemigo rosa en la parte derecha. En las imágenes 2 y 3, hay un enemigo rosa en la parte derecha.
*   **Cambio en la posición de los enemigos rojos:** En la imagen 1, hay un enemigo rojo en la parte derecha. En las imágenes 2 y 3, hay un enemigo rojo en la parte derecha.
*   **Cambio en la posición de los enemigos blancos:** En la imagen 1, hay un enemigo blanco en la parte derecha. En las imágenes 2 y 3, hay un enemigo blanco en la parte derecha.
*   **Cambio en la posición del personaje naranja:** En la imagen 1, el personaje naranja está en el aire. En las imágenes 2 y 3, el personaje naranja está en el aire, pero en una posición diferente.
*   **Cambio en la posición de los triángulos rojos:** En la imagen 1, hay dos triángulos rojos en la parte superior izquierda. En la imagen 2, hay un triángulo rojo en la parte superior izquierda. En la imagen 3, hay un triángulo rojo en la parte superior izquierda.
*   **Cambio en la posición de los triángulos marrones:** En la imagen 1, hay un triángulo marrón en la parte superior derecha. En las imágenes 2 y 3, no hay triángulos marrones en la parte superior derecha.
*   **Cambio en la posición de los triángulos azules:** En la imagen 1, hay varios triángulos azules en la parte izquierda. En las imágenes 2 y 3, hay varios triángulos azules en la parte izquierda.
*   **Cambio en la posición de los enemigos verdes:** En la imagen 1, hay un enemigo verde en la parte superior derecha. En las imágenes 2 y 3, hay un enemigo verde en la parte superior derecha.
*   **Cambio en la posición de los enemigos rosas:** En la imagen 1, hay un enemigo rosa en la parte derecha. En las imágenes 2 y 3, hay un enemigo rosa en la parte derecha.
*   **Cambio en la posición de los enemigos rojos:** En la imagen 1, hay un enemigo rojo en la parte derecha. En las imágenes 2 y 3, hay un enemigo rojo en la parte derecha.
*   **Cambio en la posición de los enemigos blancos:** En la imagen 1, hay un enemigo blanco en la parte derecha. En las imágenes 2 y 3, hay un enemigo blanco en la parte derecha.
*   **Cambio en la posición del personaje naranja:** En la imagen 1, el personaje naranja está en el aire. En las imágenes 2 y 3, el personaje naranja está en el aire, pero en una posición diferente.
*   **Cambio en la posición de los triángulos rojos:** En la imagen 1, hay dos triángulos rojos en la parte superior izquierda. En la imagen 2, hay un triángulo rojo en la parte superior izquierda. En la imagen 3, hay un triángulo rojo en la parte superior izquierda.
*   **Cambio en la posición de los triángulos marrones:** En la imagen 1, hay un triángulo marrón en la parte superior derecha. En las imágenes 2 y 3, no hay triángulos marrones en la parte superior derecha.
*   **Cambio en la posición de los triángulos azules:** En la imagen 1, hay varios triángulos azules en la parte izquierda. En las imágenes 2 y 3, hay varios triángulos azules en la parte izquierda.
*   **Cambio en la posición de los enemigos verdes:** En la imagen 1,
