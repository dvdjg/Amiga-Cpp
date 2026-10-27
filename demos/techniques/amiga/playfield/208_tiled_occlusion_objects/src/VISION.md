# EVALUACIÓN DE VISIÓN — 208_tiled_occlusion_objects

<!-- generado por tools/analyze/vision-final.mjs; no editar a mano -->

- Fecha: 2026-10-26T23:24:51.571Z
- Modelo: `qwen3-vl:8b-instruct-q8_0`
- Frames: frame_000_f0012.png, frame_003_f0015.png, frame_006_f0018.png, frame_008_f0020.png, frame_011_f0023.png (5)
- Secuencia: `out/run/208_tiled_occlusion_objects/A500_release/sequence`

## Prompt (canónico, tools/analyze/vision-prompt.txt)

```text
Las imágenes adjuntas van EN ORDEN TEMPORAL (si hay más de una). Describe con precisión qué ves: todos los elementos gráficos que aparecen, dónde están situados y qué hacen. Indica cualquier elemento que cambie entre imágenes y cualquier cosa que parezca extraña, defectuosa o incoherente (posiciones raras, parpadeos, recortes, artefactos, elementos superpuestos o incompletos). Presta especial atención a los objetos que se mueven: examina su CONTORNO en cada imagen (¿está limpio el perfil del objeto, sin banda o halo rectangular alrededor, sin restos de su posición anterior, sin píxeles cortados o colgando en los bordes?) y el FONDO inmediatamente alrededor (¿queda igual que cuando el objeto no estaba encima, o el objeto lo tapa con un rectángulo de color uniforme?). No adivines el propósito del programa: describe solo lo observable. Responde de forma concisa y ordenada.
```

## Respuesta (cruda)

Las tres imágenes son idénticas en su contenido visual y en la composición de los elementos gráficos. No hay cambios en la posición, apariencia o estado de ningún objeto entre ellas.

**Descripción de los elementos gráficos observables:**

*   **Escena general:** Vista isométrica de un entorno rural en estilo pixel art. El fondo es un cielo de color verde claro.
*   **Edificio principal:** Una casa de piedra con techo de paja, ubicada en la parte superior central. Tiene una puerta de madera, ventanas y macetas con flores en la pared.
*   **Personajes:** Dos personajes pequeños, con atuendo de color naranja y azul, están situados en el tercio izquierdo de la imagen. Uno está detrás de un banco de madera, y el otro está a su derecha, cerca de la casa.
*   **Elementos del entorno:**
    *   Un banco de madera en el tercio izquierdo.
    *   Un árbol de hoja ancha (verde) a la derecha de la casa.
    *   Un montón de heno amarillo a la derecha del árbol.
    *   Un árbol de conífera (verde) en el centro inferior.
    *   Un poste de madera con una lámpara colgante en la esquina inferior derecha.
    *   Un poste de madera con dos lámparas colgantes en la esquina superior izquierda.
    *   Un muro de piedra en la esquina superior derecha.
    *   Un camino de tierra marrón que cruza la escena.
    *   Flores blancas y rojas dispersas por el suelo.
*   **Texto en la parte superior:** Una barra de información técnica que muestra: "208 TILED OCCLUSION v1.0.6956 | F12 0.0FPS | F=12 SPLIT=1 A0=194 A1=185". En la tercera imagen, el texto cambia a "F23 19.7FPS", pero el resto permanece igual.

**Análisis de movimientos y artefactos:**

*   **Movimiento:** No hay ningún objeto que se mueva entre las imágenes. Todos los elementos están completamente estáticos.
*   **Contornos y fondo:** Todos los objetos tienen contornos limpios y definidos. No hay bandas, halos, píxeles cortados ni restos de posiciones anteriores. El fondo inmediato alrededor de cada objeto es consistente y no muestra artefactos visibles.
*   **Coherencia:** La escena es coherente y no presenta elementos superpuestos, incompletos o incoherentes. La composición es estable y completa en todas las imágenes.

En resumen, las tres imágenes muestran exactamente la misma escena estática, con la única diferencia de que el texto en la barra de información cambia de "F12 0.0FPS" a "F23 19.7FPS" en la tercera imagen, lo cual sugiere que el programa está en ejecución y ha avanzado en el tiempo, pero no ha modificado la escena visual.
