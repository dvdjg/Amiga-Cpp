# EVALUACIÓN DE VISIÓN — 080_camera_pan_window

<!-- generado por tools/analyze/vision-final.mjs; no editar a mano -->

- Fecha: 2026-10-18T12:54:31.487Z
- Modelo: `qwen3-vl:8b-instruct-q8_0`
- Frames: frame_000.png, frame_002.png, frame_004.png, frame_005.png, frame_007.png (5)
- Secuencia: `out/run/080_camera_pan_window/A500_debug/sequence`

## Prompt (canónico, tools/analyze/vision-prompt.txt)

```text
Las imágenes adjuntas van EN ORDEN TEMPORAL (si hay más de una). Describe con precisión qué ves: todos los elementos gráficos que aparecen, dónde están situados y qué hacen. Indica cualquier elemento que cambie entre imágenes y cualquier cosa que parezca extraña, defectuosa o incoherente (posiciones raras, parpadeos, recortes, artefactos, elementos superpuestos o incompletos). Presta especial atención a los objetos que se mueven: examina su CONTORNO en cada imagen (¿está limpio el perfil del objeto, sin banda o halo rectangular alrededor, sin restos de su posición anterior, sin píxeles cortados o colgando en los bordes?) y el FONDO inmediatamente alrededor (¿queda igual que cuando el objeto no estaba encima, o el objeto lo tapa con un rectángulo de color uniforme?). No adivines el propósito del programa: describe solo lo observable. Responde de forma concisa y ordenada.
```

## Respuesta (cruda)

**Imagen 1:**
- **Contenido visual:** Una ventana de visualización con un fondo de patrones de rayas diagonales en tonos azules, verdes y marrones, con un efecto de pixelización. En el centro, hay cinco barras verticales de colores: dos cian, una amarilla, una naranja y otra cian (de izquierda a derecha). Las barras están dispuestas en una fila horizontal.
- **Texto en la parte inferior:** "080 CAMERA PAN WINDOW", "FULL 12.5 + FAKE 25", "FRAME 02 FULL 115 FAKE 20", "FAKE OFF 0 FAC 7 VER 39 PAN -128KK22".
- **Estado:** La ventana muestra una vista completa de las barras y el fondo.

**Imagen 2:**
- **Contenido visual:** La ventana de visualización muestra el mismo fondo de rayas diagonales. Las cinco barras verticales están presentes, pero su posición ha cambiado: ahora están más centradas horizontalmente, y la barra naranja está más a la derecha.
- **Texto en la parte inferior:** "080 CAMERA PAN WINDOW", "FULL 12.5 + FAKE 25", "FRAME 96 FULL 35 FAKE 62", "FULL OFF 0 FAC 9 VER 43 PAN 13 K 2".
- **Estado:** Las barras han sido desplazadas hacia la derecha y hacia el centro. El texto en la parte inferior ha cambiado, indicando un cambio en los parámetros de la cámara.

**Imagen 3:**
- **Contenido visual:** La ventana de visualización es idéntica a la de la imagen 2. Las cinco barras verticales mantienen la misma posición que en la imagen 2.
- **Texto en la parte inferior:** "080 CAMERA PAN WINDOW", "FULL 12.5 + FAKE 25", "FRAME 96 FULL 35 FAKE 62", "FULL OFF 0 FAC 9 VER 43 PAN 13 K 2".
- **Estado:** La imagen es exactamente igual a la imagen 2. No hay cambios visibles en la posición de las barras ni en el texto.

**Cambios observados:**
- Las barras verticales se desplazan de la imagen 1 a la imagen 2, y permanecen en la misma posición en la imagen 3.
- El texto en la parte inferior cambia de la imagen 1 a la imagen 2, y permanece igual en la imagen 3.
- No hay artefactos visibles, como bandas, halos o píxeles cortados. Los contornos de las barras son limpios y no hay restos de su posición anterior. El fondo se ve uniforme y no hay elementos superpuestos.
