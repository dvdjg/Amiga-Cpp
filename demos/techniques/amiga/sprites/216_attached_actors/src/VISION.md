# EVALUACIÓN DE VISIÓN — 216_attached_actors

<!-- generado por tools/analyze/vision-final.mjs; no editar a mano -->

- Fecha: 2026-10-18T19:53:25.158Z
- Modelo: `qwen3-vl:8b-instruct-q8_0`
- Frames: frame_000.png, frame_002.png, frame_003.png, frame_005.png (4)
- Secuencia: `out/run/216_attached_actors/A500_debug/sequence`

## Prompt (canónico, tools/analyze/vision-prompt.txt)

```text
Las imágenes adjuntas van EN ORDEN TEMPORAL (si hay más de una). Describe con precisión qué ves: todos los elementos gráficos que aparecen, dónde están situados y qué hacen. Indica cualquier elemento que cambie entre imágenes y cualquier cosa que parezca extraña, defectuosa o incoherente (posiciones raras, parpadeos, recortes, artefactos, elementos superpuestos o incompletos). Presta especial atención a los objetos que se mueven: examina su CONTORNO en cada imagen (¿está limpio el perfil del objeto, sin banda o halo rectangular alrededor, sin restos de su posición anterior, sin píxeles cortados o colgando en los bordes?) y el FONDO inmediatamente alrededor (¿queda igual que cuando el objeto no estaba encima, o el objeto lo tapa con un rectángulo de color uniforme?). No adivines el propósito del programa: describe solo lo observable. Responde de forma concisa y ordenada.
```

## Respuesta (cruda)

**Imagen 1:**

*   **Fondo:** Una composición de capas horizontales de bloques pixelados.
    *   Capa superior: Bloques oscuros (negro y azul marino).
    *   Capa media: Bloques de tonos de azul turquesa, con un patrón de cuadrícula.
    *   Capa inferior: Bloques grises oscuros, con un patrón de cuadrícula.
    *   Base: Una franja blanca con un patrón de cuadrícula.
*   **Objetos móviles (en la parte superior derecha):**
    *   Un objeto azul brillante (forma de rombo o cuadrado con bordes redondeados) en la parte superior derecha.
    *   Debajo de él, un objeto con un contorno de color naranja y un interior de color rosa y azul.
    *   Más abajo, un objeto con un contorno de color naranja y un interior de color rosa y azul.
    *   A la derecha de estos, un objeto azul brillante (forma de rombo o cuadrado con bordes redondeados).
*   **Objetos móviles (en la parte inferior izquierda):**
    *   Un objeto con un contorno de color naranja y un interior de color rosa y azul.
    *   A su derecha, un objeto azul brillante (forma de rombo o cuadrado con bordes redondeados).
    *   A la derecha de este, un objeto azul brillante (forma de rombo o cuadrado con bordes redondeados).

**Imagen 2:**

*   **Fondo:** El fondo es idéntico al de la primera imagen.
*   **Objetos móviles (en la parte superior izquierda):**
    *   Un objeto con un contorno de color naranja y un interior de color rosa y azul.
    *   A su derecha, un objeto azul brillante (forma de rombo o cuadrado con bordes redondeados).
    *   A la derecha de este, un objeto azul brillante (forma de rombo o cuadrado con bordes redondeados).
*   **Objetos móviles (en la parte inferior derecha):**
    *   Un objeto azul brillante (forma de rombo o cuadrado con bordes redondeados).
    *   A su izquierda, un objeto con un contorno de color naranja y un interior de color rosa y azul.
    *   A la izquierda de este, un objeto con un contorno de color naranja y un interior de color rosa y azul.

**Cambios entre imágenes:**

*   **Posición de los objetos:** Los objetos se han movido de posición. En la primera imagen, los objetos están agrupados en la parte superior derecha y en la parte inferior izquierda. En la segunda imagen, los objetos están agrupados en la parte superior izquierda y en la parte inferior derecha.
*   **Contorno de los objetos:** Los contornos de los objetos son limpios, sin bandas ni halos rectangulares alrededor. No hay píxeles cortados o colgando en los bordes.
*   **Fondo alrededor de los objetos:** El fondo inmediatamente alrededor de los objetos es el mismo que cuando no están encima, y los objetos los tapan completamente sin dejar rastros de su posición anterior.
*   **Elementos extraños o incoherentes:** No se observan elementos extraños, defectuosos o incoherentes. La composición es consistente y los objetos se mueven de forma fluida entre las imágenes.
