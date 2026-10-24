# EVALUACIÓN DE VISIÓN — 092_articulated_character

<!-- generado por tools/analyze/vision-final.mjs; no editar a mano -->

- Fecha: 2026-10-24T07:39:38.836Z
- Modelo: `qwen3-vl:8b-instruct-q8_0`
- Frames: frame_000.png, frame_001.png, frame_002.png, frame_003.png (4)
- Secuencia: `out/run/092_articulated_character/A500_debug/sequence`

## Prompt (canónico, tools/analyze/vision-prompt.txt)

```text
Las imágenes adjuntas van EN ORDEN TEMPORAL (si hay más de una). Describe con precisión qué ves: todos los elementos gráficos que aparecen, dónde están situados y qué hacen. Indica cualquier elemento que cambie entre imágenes y cualquier cosa que parezca extraña, defectuosa o incoherente (posiciones raras, parpadeos, recortes, artefactos, elementos superpuestos o incompletos). Presta especial atención a los objetos que se mueven: examina su CONTORNO en cada imagen (¿está limpio el perfil del objeto, sin banda o halo rectangular alrededor, sin restos de su posición anterior, sin píxeles cortados o colgando en los bordes?) y el FONDO inmediatamente alrededor (¿queda igual que cuando el objeto no estaba encima, o el objeto lo tapa con un rectángulo de color uniforme?). No adivines el propósito del programa: describe solo lo observable. Responde de forma concisa y ordenada.
```

## Respuesta (cruda)

**Imagen 1:**
- **Elementos gráficos:** Un conjunto de tres formas geométricas sobre fondo negro.
  - Una forma rectangular vertical de color azul claro (cielo).
  - Una forma rectangular horizontal de color azul más oscuro (agua), conectada a la parte inferior derecha de la forma vertical.
  - Una forma rectangular vertical de color naranja (tierra), situada debajo de la forma azul más oscuro, con su borde superior alineado con el borde inferior de la forma azul más oscuro.
- **Posición:** Las formas están centradas horizontalmente en la imagen, ligeramente desplazadas hacia la derecha.
- **Estado:** Las formas están completamente visibles, con bordes limpios y sin artefactos. El fondo negro es uniforme y no muestra restos de posiciones anteriores.
- **Texto en la parte superior:** "692 ARTICULATED CHARACTER v1.0.6300 | F1 301.0FPS | P3 35B3F3".

**Imagen 2:**
- **Elementos gráficos:** Las mismas tres formas geométricas, en la misma posición y con los mismos colores.
- **Posición:** Las formas están en la misma posición que en la imagen 1.
- **Estado:** Las formas están completamente visibles, con bordes limpios y sin artefactos. El fondo negro es uniforme y no muestra restos de posiciones anteriores.
- **Texto en la parte superior:** "692 ARTICULATED CHARACTER v1.0.6300 | F2 11.7FPS | P3 53 F3".

**Cambios observados:**
- El texto en la parte superior cambia entre las dos imágenes, indicando una diferencia en los valores de rendimiento (FPS) y posiblemente en el estado del sistema o la configuración.
- Las formas gráficas y su posición no cambian entre las dos imágenes.

**Observaciones:**
- No hay movimiento ni cambio en la posición de los objetos entre las dos imágenes.
- Los bordes de las formas son limpios y no presentan halos, bandas ni píxeles cortados.
- El fondo negro es uniforme y no muestra artefactos ni restos de posiciones anteriores.
- El conjunto de formas parece estar compuesto por tres bloques de colores distintos, posiblemente representando una estructura o figura abstracta.
