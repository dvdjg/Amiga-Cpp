# EVALUACIÓN DE VISIÓN — 096_silhouette_swarm

<!-- generado por tools/analyze/vision-final.mjs; no editar a mano -->

- Fecha: 2026-10-24T12:18:18.223Z
- Modelo: `qwen3-vl:8b-instruct-q8_0`
- Frames: frame_000.png, frame_001.png, frame_002.png, frame_003.png (4)
- Secuencia: `out/run/096_silhouette_swarm/A500_release/sequence`

## Prompt (canónico, tools/analyze/vision-prompt.txt)

```text
Las imágenes adjuntas van EN ORDEN TEMPORAL (si hay más de una). Describe con precisión qué ves: todos los elementos gráficos que aparecen, dónde están situados y qué hacen. Indica cualquier elemento que cambie entre imágenes y cualquier cosa que parezca extraña, defectuosa o incoherente (posiciones raras, parpadeos, recortes, artefactos, elementos superpuestos o incompletos). Presta especial atención a los objetos que se mueven: examina su CONTORNO en cada imagen (¿está limpio el perfil del objeto, sin banda o halo rectangular alrededor, sin restos de su posición anterior, sin píxeles cortados o colgando en los bordes?) y el FONDO inmediatamente alrededor (¿queda igual que cuando el objeto no estaba encima, o el objeto lo tapa con un rectángulo de color uniforme?). No adivines el propósito del programa: describe solo lo observable. Responde de forma concisa y ordenada.
```

## Respuesta (cruda)

**Imagen 1:**
- **Fondo:** Negro uniforme.
- **Elementos gráficos:**
  - En la parte superior, cuatro formas naranjas, que parecen fragmentos de cuadrados o rectángulos, dispuestos en línea horizontal. La forma central es más pequeña y tiene un recorte en su parte inferior izquierda.
  - En la parte inferior central, un objeto azul claro de forma irregular, similar a un hexágono distorsionado o un polígono con bordes suaves, que parece estar girando ligeramente.
- **Texto en la parte superior izquierda:** `096 SILHOUETTE SWARM v1.0.6363 | F26 84.0FPS | 55CLO2D2`.
- **Estado general:** Todos los elementos están visibles y completos. No hay artefactos visibles en los bordes de los objetos ni en el fondo.

**Imagen 2:**
- **Fondo:** Negro uniforme.
- **Elementos gráficos:**
  - Las cuatro formas naranjas en la parte superior están en la misma posición y forma que en la imagen 1.
  - El objeto azul claro en la parte inferior central ha cambiado ligeramente de posición y orientación, girado un poco hacia la derecha y hacia abajo, lo que sugiere movimiento.
- **Texto en la parte superior izquierda:** `096 SILHOUETTE SWARM v1.0.6363 | F50 25.3FPS | SIL 02 D2`.
- **Estado general:** El objeto azul parece estar en movimiento, pero su contorno es limpio y no hay artefactos ni restos de su posición anterior. El fondo alrededor de los objetos es uniformemente negro, sin restos de color o textura.

**Cambios entre imágenes:**
- El objeto azul en la parte inferior cambia de posición y orientación, indicando movimiento.
- El texto en la parte superior cambia: la indicación de FPS cambia de `84.0FPS` a `25.3FPS`, y el texto a la derecha cambia de `55CLO2D2` a `SIL 02 D2`.
- Las formas naranjas en la parte superior permanecen estacionarias y sin cambios.

**Observaciones:**
- Los objetos tienen contornos limpios, sin halos ni artefactos visibles.
- El fondo es uniformemente negro en ambos casos.
- No hay elementos superpuestos ni incompletos.
- La posición de los objetos naranjas es idéntica en ambas imágenes.
