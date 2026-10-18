# EVALUACIÓN DE VISIÓN — 080_camera_pan_window

<!-- generado por tools/analyze/vision-final.mjs; no editar a mano -->

- Fecha: 2026-10-18T16:45:33.314Z
- Modelo: `qwen3-vl:8b-instruct-q8_0`
- Frames: frame_000.png, frame_001.png, frame_002.png, frame_003.png, frame_004.png (5)
- Secuencia: `out/run/080_camera_pan_window/A500_debug/sequence`

## Prompt (canónico, tools/analyze/vision-prompt.txt)

```text
Las imágenes adjuntas van EN ORDEN TEMPORAL (si hay más de una). Describe con precisión qué ves: todos los elementos gráficos que aparecen, dónde están situados y qué hacen. Indica cualquier elemento que cambie entre imágenes y cualquier cosa que parezca extraña, defectuosa o incoherente (posiciones raras, parpadeos, recortes, artefactos, elementos superpuestos o incompletos). Presta especial atención a los objetos que se mueven: examina su CONTORNO en cada imagen (¿está limpio el perfil del objeto, sin banda o halo rectangular alrededor, sin restos de su posición anterior, sin píxeles cortados o colgando en los bordes?) y el FONDO inmediatamente alrededor (¿queda igual que cuando el objeto no estaba encima, o el objeto lo tapa con un rectángulo de color uniforme?). No adivines el propósito del programa: describe solo lo observable. Responde de forma concisa y ordenada.
```

## Respuesta (cruda)

**Observación de las imágenes en orden temporal:**

**Imagen 1:**
- **Elementos gráficos:** Una ventana de visualización con un fondo azul oscuro y un patrón de malla de puntos. Dentro de la ventana, hay cinco barras verticales de colores: dos cian, una amarilla, una cian y una naranja, dispuestas en fila horizontal. A ambos lados de las barras, hay dos grandes triángulos que parecen paredes o planos inclinados, uno verde oscuro a la izquierda y uno marrón a la derecha. El fondo de la ventana tiene un patrón de malla de puntos azul oscuro.
- **Texto en la parte inferior:** "080 CAMERA PAN WINDOW", "FULL 12.5 + FAKE 25", "FRAME 0 FULL 1 FAKE 0", "FULL OFF 0 FAC 7 VER 28 PAN -128 K 2".
- **Estado de los objetos:** Las barras están completamente visibles dentro del marco de la ventana. No hay artefactos visibles en los bordes de los objetos ni en el fondo.

**Imagen 2:**
- **Cambios observables:**
  - Las barras se han desplazado ligeramente hacia la derecha.
  - El texto en la parte inferior ha cambiado: "FRAME 62 FULL 28 FAKE 20", "FAKE OFF 2 FAC 9 VER 39 PAN -51 K 2".
  - El valor de "PAN" ha cambiado de -128 a -51, lo que indica un movimiento de la cámara hacia la derecha.
  - El valor de "FAKE OFF" ha cambiado de 0 a 2.
  - El valor de "VER" ha cambiado de 28 a 39.
  - El valor de "FAC" ha cambiado de 7 a 9.
- **Estado de los objetos:** Las barras están completamente visibles dentro del marco de la ventana. No hay artefactos visibles en los bordes de los objetos ni en el fondo.

**Imagen 3:**
- **Cambios observables:**
  - Las barras se han desplazado aún más hacia la derecha.
  - El texto en la parte inferior ha cambiado: "FRAME 64 FULL 24 FAKE 41", "FAKE OFF 1 FAC 9 VER 39 PAN -19 K 2".
  - El valor de "PAN" ha cambiado de -51 a -19, lo que indica un movimiento de la cámara hacia la derecha.
  - El valor de "FAKE OFF" ha cambiado de 2 a 1.
  - El valor de "FULL" ha cambiado de 1 a 24.
  - El valor de "FAKE" ha cambiado de 20 a 41.
- **Estado de los objetos:** Las barras están completamente visibles dentro del marco de la ventana. No hay artefactos visibles en los bordes de los objetos ni en el fondo.

**Conclusión:**
- Las imágenes muestran una secuencia de movimiento de la cámara (pan) hacia la derecha, lo que se refleja en el cambio de los valores de "PAN" y "FAKE OFF" en el texto de la parte inferior.
- Los objetos (barras) se desplazan de manera coherente con el movimiento de la cámara.
- No hay artefactos visibles en los bordes de los objetos ni en el fondo.
- El fondo de la ventana tiene un patrón de malla de puntos azul oscuro, que permanece constante en todas las imágenes.
- El texto en la parte inferior cambia en cada imagen, reflejando los parámetros de la cámara y el estado de los objetos.
