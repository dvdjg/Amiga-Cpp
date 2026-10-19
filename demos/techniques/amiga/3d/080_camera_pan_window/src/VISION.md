# EVALUACIÓN DE VISIÓN — 080_camera_pan_window

<!-- generado por tools/analyze/vision-final.mjs; no editar a mano -->

- Fecha: 2026-10-19T11:42:30.426Z
- Modelo: `qwen3-vl:8b-instruct-q8_0`
- Frames: frame_000.png, frame_002.png, frame_005.png, frame_007.png, frame_009.png (5)
- Secuencia: `out/run/080_camera_pan_window/A500_debug/sequence`

## Prompt (canónico, tools/analyze/vision-prompt.txt)

```text
Las imágenes adjuntas van EN ORDEN TEMPORAL (si hay más de una). Describe con precisión qué ves: todos los elementos gráficos que aparecen, dónde están situados y qué hacen. Indica cualquier elemento que cambie entre imágenes y cualquier cosa que parezca extraña, defectuosa o incoherente (posiciones raras, parpadeos, recortes, artefactos, elementos superpuestos o incompletos). Presta especial atención a los objetos que se mueven: examina su CONTORNO en cada imagen (¿está limpio el perfil del objeto, sin banda o halo rectangular alrededor, sin restos de su posición anterior, sin píxeles cortados o colgando en los bordes?) y el FONDO inmediatamente alrededor (¿queda igual que cuando el objeto no estaba encima, o el objeto lo tapa con un rectángulo de color uniforme?). No adivines el propósito del programa: describe solo lo observable. Responde de forma concisa y ordenada.
```

## Respuesta (cruda)

Las tres imágenes muestran una secuencia de captura de pantalla de un programa de edición de video o animación, probablemente de una computadora antigua, que visualiza un efecto de cámara que se desplaza (pan) a través de una escena. El texto en la parte inferior proporciona parámetros técnicos del efecto.

**Descripción general de la escena:**
- La escena es una representación 2D simplificada de un pasillo o espacio con paredes laterales y un suelo.
- Las paredes laterales son de color verde oscuro (izquierda) y marrón (derecha).
- El techo es de color azul oscuro.
- El suelo es de color marrón claro.
- En el centro de la escena, hay cinco barras verticales de colores: cian, amarillo, cian, azul, cian y naranja (de izquierda a derecha).

**Cambio entre imágenes:**
- **Imagen 1:** La cámara está posicionada a la izquierda, mostrando la pared verde y las cinco barras.
- **Imagen 2:** La cámara se ha desplazado hacia la derecha, mostrando menos de la pared verde y más de la pared marrón. Las barras están más centradas.
- **Imagen 3:** La cámara se ha desplazado aún más hacia la derecha, casi completamente fuera de la pared verde. Las barras están más a la izquierda del encuadre.

**Elementos gráficos y texto:**
- **Texto en la parte inferior:** Muestra parámetros del efecto de cámara, como "FULL", "FAKE", "FRAME", "FAC", "VER", "PAN", y "K". Estos valores cambian entre las imágenes, indicando el progreso del efecto.
- **Contorno de los objetos:** Los contornos de las barras y las paredes son limpios, sin halos ni artefactos visibles. No hay píxeles cortados ni restos de posiciones anteriores.
- **Fondo:** El fondo inmediato alrededor de los objetos es uniforme y consistente con el color de las paredes y el suelo. No hay artefactos ni superposiciones.

**Observaciones:**
- La secuencia muestra un desplazamiento progresivo de la cámara hacia la derecha.
- El texto en la parte inferior cambia de forma coherente con el desplazamiento, reflejando el progreso del efecto.
- No hay elementos extraños, defectuosos o incoherentes en la representación gráfica. La escena es consistente y bien definida en cada imagen.
