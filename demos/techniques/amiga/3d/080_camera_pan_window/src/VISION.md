# EVALUACIÓN DE VISIÓN — 080_camera_pan_window

<!-- generado por tools/analyze/vision-final.mjs; no editar a mano -->

- Fecha: 2026-10-19T12:20:25.774Z
- Modelo: `qwen3-vl:8b-instruct-q8_0`
- Frames: frame_000.png, frame_010.png, frame_020.png, frame_029.png, frame_039.png (5)
- Secuencia: `out/run/080_camera_pan_window/A500_debug/sequence`

## Prompt (canónico, tools/analyze/vision-prompt.txt)

```text
Las imágenes adjuntas van EN ORDEN TEMPORAL (si hay más de una). Describe con precisión qué ves: todos los elementos gráficos que aparecen, dónde están situados y qué hacen. Indica cualquier elemento que cambie entre imágenes y cualquier cosa que parezca extraña, defectuosa o incoherente (posiciones raras, parpadeos, recortes, artefactos, elementos superpuestos o incompletos). Presta especial atención a los objetos que se mueven: examina su CONTORNO en cada imagen (¿está limpio el perfil del objeto, sin banda o halo rectangular alrededor, sin restos de su posición anterior, sin píxeles cortados o colgando en los bordes?) y el FONDO inmediatamente alrededor (¿queda igual que cuando el objeto no estaba encima, o el objeto lo tapa con un rectángulo de color uniforme?). No adivines el propósito del programa: describe solo lo observable. Responde de forma concisa y ordenada.
```

## Respuesta (cruda)

Las tres imágenes muestran una secuencia de captura de pantalla de un programa de animación 3D, probablemente de una computadora antigua, que muestra una escena con columnas y una interfaz de control en la parte inferior.

**Elementos gráficos observables:**

*   **Escena 3D:** En el centro de cada imagen, hay una representación de una escena en perspectiva. Contiene:
    *   **Columnas:** Cuatro columnas verticales, dos a la izquierda y dos a la derecha. Las columnas de la izquierda son cian y amarillas; las de la derecha son cian y naranjas. Las columnas están dispuestas en una fila.
    *   **Suelo y paredes:** El suelo es de color marrón. Las paredes laterales son de color verde oscuro. El fondo es de un azul oscuro.
    *   **Líneas de referencia:** Hay dos líneas horizontales delgadas que cruzan la escena, una cerca del suelo y otra más arriba, que parecen ser líneas de referencia para la cámara o el sistema de coordenadas.
*   **Interfaz de texto:** En la parte inferior de cada imagen, hay una línea de texto en blanco sobre fondo negro que muestra parámetros de la cámara y el estado de la animación.

**Cambios entre imágenes:**

*   **Posición de la cámara (Pan):** La cámara se mueve horizontalmente (pan) hacia la derecha en la secuencia.
    *   En la imagen 1, la cámara está más a la izquierda, mostrando más de la pared izquierda y menos de la derecha.
    *   En la imagen 2, la cámara se ha movido hacia la derecha, mostrando más de la pared derecha y menos de la izquierda.
    *   En la imagen 3, la cámara está aún más a la derecha, casi centrada en las columnas de la derecha.
*   **Parámetros de la interfaz:**
    *   **FRAME:** El número de fotogramas aumenta progresivamente: 96 → 250 → 352.
    *   **PAN:** El valor de "PAN" cambia de +12K a +77K y luego a -87K, lo que indica un movimiento de la cámara hacia la derecha, luego hacia la izquierda, y nuevamente hacia la derecha.
    *   **K:** El valor de "K" cambia de 1 a 2, lo que probablemente indica un cambio en la configuración de la cámara o el modo de renderizado.
    *   **FAKE OFF:** El valor cambia de 0 a 12 y luego a -2, lo que sugiere un cambio en el modo de renderizado o en la configuración de la cámara.

**Elementos extraños o incoherentes:**

*   **Artefactos visuales:** Las imágenes muestran un ligero efecto de "banda" o "halo" alrededor de los bordes de los objetos, especialmente en las columnas y las líneas de referencia. Esto es típico de la resolución baja y la interpolación de píxeles de las computadoras de la época.
*   **Recortes y píxeles cortados:** No hay recortes visibles de objetos, pero los bordes de los objetos y las líneas de referencia no son completamente limpios, lo que sugiere que el sistema de renderizado no es perfecto.
*   **Fondo:** El fondo permanece uniforme en cada imagen, sin artefactos visibles.

**Conclusión:**

Las imágenes muestran una secuencia de animación 3D en la que la cámara se mueve horizontalmente (pan) a través de una escena con columnas. La interfaz de texto muestra los parámetros de la cámara y el estado de la animación, que cambian de forma coherente con el movimiento de la cámara. Los artefactos visuales son típicos de la tecnología de la época.
