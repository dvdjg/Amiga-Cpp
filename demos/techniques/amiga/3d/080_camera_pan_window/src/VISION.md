# EVALUACIÓN DE VISIÓN — 080_camera_pan_window

<!-- generado por tools/analyze/vision-final.mjs; no editar a mano -->

- Fecha: 2026-10-21T19:02:10.116Z
- Modelo: `qwen3-vl:8b-instruct-q8_0`
- Frames: frame_000.png, frame_086.png, frame_173.png, frame_259.png (4)
- Secuencia: `out/run/080_camera_pan_window/A500_debug/sequence`

## Prompt (canónico, tools/analyze/vision-prompt.txt)

```text
Las imágenes adjuntas van EN ORDEN TEMPORAL (si hay más de una). Describe con precisión qué ves: todos los elementos gráficos que aparecen, dónde están situados y qué hacen. Indica cualquier elemento que cambie entre imágenes y cualquier cosa que parezca extraña, defectuosa o incoherente (posiciones raras, parpadeos, recortes, artefactos, elementos superpuestos o incompletos). Presta especial atención a los objetos que se mueven: examina su CONTORNO en cada imagen (¿está limpio el perfil del objeto, sin banda o halo rectangular alrededor, sin restos de su posición anterior, sin píxeles cortados o colgando en los bordes?) y el FONDO inmediatamente alrededor (¿queda igual que cuando el objeto no estaba encima, o el objeto lo tapa con un rectángulo de color uniforme?). No adivines el propósito del programa: describe solo lo observable. Responde de forma concisa y ordenada.
```

## Respuesta (cruda)

**Imagen 1:**

*   **Contenido visual:** Una escena 3D de estilo pixel art con una perspectiva isométrica. El suelo es de color marrón claro. Las paredes laterales son de un azul oscuro. El techo es de un verde oscuro. En el centro de la escena hay varios objetos geométricos: dos columnas verticales (una amarilla, una naranja), un objeto cúbico amarillo a la izquierda, y un objeto más complejo (parecido a un cubo con una sección azul cian y una sección naranja) en el centro-derecha.
*   **Texto en la parte inferior:** Una barra negra con texto blanco que muestra información de cámara: `080 CAMERA PAN WINDOW`, `FULL 12.5 + FAKE 25`, `FRAME 576UELLO FAREFAKE 433`, `MODE YAW Y 08PP00FF103KK33`.
*   **Estado de los objetos:** Todos los objetos están completamente visibles, con contornos limpios y sin artefactos visibles. No hay píxeles cortados ni restos de posiciones anteriores. El fondo inmediato alrededor de los objetos es uniforme y no muestra artefactos.

**Imagen 2:**

*   **Contenido visual:** La escena ha cambiado. El suelo es ahora de un verde oliva. Las paredes laterales son de un azul más claro. El techo es de un azul más oscuro. Los objetos geométricos están ahora en la parte superior de la imagen: una columna amarilla a la izquierda, una columna naranja a la derecha, y un objeto cúbico azul cian en el centro-derecha.
*   **Texto en la parte inferior:** La barra negra con texto blanco muestra información de cámara diferente: `080 CAMERA PAN WINDOW`, `FULL 12.5 + FAKE 25`, `FRAME 1152 FULL 293 FAKE 2606`, `MODE PITCH Y 0 P 134FF15 K 3`.
*   **Estado de los objetos:** Los objetos están completamente visibles, con contornos limpios. No hay artefactos visibles. El fondo inmediato alrededor de los objetos es uniforme y no muestra artefactos.

**Cambios entre imágenes:**

*   **Posición de los objetos:** Los objetos se han desplazado hacia arriba en la escena, y su disposición ha cambiado.
*   **Color del suelo:** El suelo cambia de marrón claro a verde oliva.
*   **Color de las paredes y techo:** Las paredes y el techo cambian de color.
*   **Texto de la cámara:** El texto en la parte inferior cambia completamente, indicando un cambio en el modo de la cámara (de `YAW` a `PITCH`) y en los valores de frame y posición.
