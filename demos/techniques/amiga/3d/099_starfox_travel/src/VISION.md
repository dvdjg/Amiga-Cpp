# EVALUACIÓN DE VISIÓN — 099_starfox_travel

<!-- generado por tools/analyze/vision-final.mjs; no editar a mano -->

- Fecha: 2026-10-25T06:59:06.723Z
- Modelo: `qwen3-vl:8b-instruct-q8_0`
- Frames: frame_000.png, frame_004.png, frame_007.png (3)
- Secuencia: `out/run/099_starfox_travel/A500_release/sequence`

## Prompt (canónico, tools/analyze/vision-prompt.txt)

```text
Las imágenes adjuntas van EN ORDEN TEMPORAL (si hay más de una). Describe con precisión qué ves: todos los elementos gráficos que aparecen, dónde están situados y qué hacen. Indica cualquier elemento que cambie entre imágenes y cualquier cosa que parezca extraña, defectuosa o incoherente (posiciones raras, parpadeos, recortes, artefactos, elementos superpuestos o incompletos). Presta especial atención a los objetos que se mueven: examina su CONTORNO en cada imagen (¿está limpio el perfil del objeto, sin banda o halo rectangular alrededor, sin restos de su posición anterior, sin píxeles cortados o colgando en los bordes?) y el FONDO inmediatamente alrededor (¿queda igual que cuando el objeto no estaba encima, o el objeto lo tapa con un rectángulo de color uniforme?). No adivines el propósito del programa: describe solo lo observable. Responde de forma concisa y ordenada.
```

## Respuesta (cruda)

**Análisis de las imágenes:**

**Elementos gráficos observables:**

*   **Fondo:** Un paisaje pixelado compuesto por tres bandas horizontales: una superior de color azul oscuro, una media de azul claro y una inferior de verde. Una franja de agua azul oscuro ocupa la parte inferior de la pantalla, separada del terreno verde por una línea diagonal.
*   **Objetos en el terreno:**
    *   A la izquierda: Un bloque rectangular gris.
    *   Al centro: Una estructura de arco de color marrón claro, con un pequeño triángulo rojo en su interior, apuntando hacia arriba.
    *   En el centro, frente al arco: Un objeto que parece ser un vehículo o nave espacial, con forma de triángulo azul y un cuerpo blanco con un detalle azul en la parte superior.
*   **Texto en la parte inferior:** Una línea de texto en blanco sobre fondo negro que dice: `099 STARFOX TRAVEL` en la primera línea y `RAIL RUN CORNERIA NO INPUT` en la segunda.

**Diferencias entre las imágenes:**

*   **Posición del objeto central:** En la primera imagen, el objeto azul y blanco está posicionado justo debajo del arco, con su parte frontal apuntando hacia la derecha. En la segunda imagen, el objeto ha avanzado ligeramente hacia la derecha, ahora parcialmente superpuesto al arco, con su parte frontal aún apuntando hacia la derecha.
*   **Contorno del objeto:** En ambas imágenes, el contorno del objeto es limpio, sin bandas ni halos rectangulares. No hay píxeles cortados ni colgando en los bordes.
*   **Fondo alrededor del objeto:** El fondo inmediatamente alrededor del objeto permanece igual en ambas imágenes. No hay artefactos visibles ni cambios en el fondo.
*   **Otras partes de la imagen:** El resto de la escena, incluyendo el fondo, los bloques y el texto, permanece completamente inalterado entre las dos imágenes.

**Observaciones:**

*   La única diferencia observable es el desplazamiento del objeto central hacia la derecha en la segunda imagen.
*   El texto en la parte inferior es idéntico en ambas imágenes.
*   No hay elementos superpuestos ni recortes visibles.
*   La escena parece ser una captura de pantalla de un videojuego, posiblemente de estilo retro o de simulación, donde el objeto se mueve en una trayectoria lineal.
