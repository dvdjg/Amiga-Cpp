# EVALUACIÓN DE VISIÓN — 215_app_bobs

<!-- generado por tools/analyze/vision-final.mjs; no editar a mano -->

- Fecha: 2026-10-18T19:43:36.681Z
- Modelo: `qwen3-vl:8b-instruct-q8_0`
- Frames: frame_000.png, frame_002.png, frame_003.png, frame_005.png (4)
- Secuencia: `out/run/215_app_bobs/A500_debug/sequence`

## Prompt (canónico, tools/analyze/vision-prompt.txt)

```text
Las imágenes adjuntas van EN ORDEN TEMPORAL (si hay más de una). Describe con precisión qué ves: todos los elementos gráficos que aparecen, dónde están situados y qué hacen. Indica cualquier elemento que cambie entre imágenes y cualquier cosa que parezca extraña, defectuosa o incoherente (posiciones raras, parpadeos, recortes, artefactos, elementos superpuestos o incompletos). Presta especial atención a los objetos que se mueven: examina su CONTORNO en cada imagen (¿está limpio el perfil del objeto, sin banda o halo rectangular alrededor, sin restos de su posición anterior, sin píxeles cortados o colgando en los bordes?) y el FONDO inmediatamente alrededor (¿queda igual que cuando el objeto no estaba encima, o el objeto lo tapa con un rectángulo de color uniforme?). No adivines el propósito del programa: describe solo lo observable. Responde de forma concisa y ordenada.
```

## Respuesta (cruda)

**Imagen 1:**

- **Fondo:** Un fondo de color azul marino uniforme.
- **Elementos gráficos:**
  - **Cuadrado rojo pequeño:** En la esquina superior izquierda.
  - **Cuadrado rojo mediano:** En el centro izquierdo.
  - **Cuadrado amarillo mediano:** En la parte superior derecha.
  - **Cuadrado amarillo pequeño:** En la parte superior derecha, más a la derecha que el anterior.
  - **Grupo de dos cuadrados en la esquina inferior derecha:**
    - Un cuadrado amarillo.
    - Un cuadrado rojo, superpuesto parcialmente sobre el amarillo, en la parte inferior derecha del grupo.

**Imagen 2:**

- **Fondo:** El mismo fondo azul marino uniforme.
- **Elementos gráficos:**
  - **Cuadrado rojo pequeño:** En el centro izquierdo.
  - **Cuadrado amarillo mediano:** En la parte inferior izquierda.
  - **Cuadrado amarillo pequeño:** En la parte superior derecha.
  - **Grupo de dos cuadrados en la esquina inferior derecha:**
    - Un cuadrado amarillo.
    - Un cuadrado rojo, superpuesto parcialmente sobre el amarillo, en la parte inferior derecha del grupo.
  - **Cuadrado rojo pequeño:** En la esquina inferior derecha, fuera del grupo anterior, casi tocando el borde derecho de la imagen.

**Cambios entre imágenes:**

- El cuadrado rojo en la esquina superior izquierda desaparece.
- El cuadrado rojo mediano en el centro izquierdo se mueve hacia el centro.
- El cuadrado amarillo mediano en la parte superior derecha desaparece.
- El cuadrado amarillo mediano en la parte inferior izquierda aparece.
- Un nuevo cuadrado rojo pequeño aparece en la esquina inferior derecha.
- El grupo de dos cuadrados en la esquina inferior derecha se desplaza ligeramente hacia la derecha y hacia abajo.

**Observaciones:**

- Todos los objetos tienen contornos limpios, sin halos ni artefactos.
- No hay píxeles cortados ni restos de posiciones anteriores.
- El fondo permanece uniforme y no muestra artefactos alrededor de los objetos.
- Los objetos superpuestos (amarillo y rojo en la esquina inferior derecha) mantienen su relación de superposición en ambas imágenes.
