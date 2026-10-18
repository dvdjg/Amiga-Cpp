# EVALUACIÓN DE VISIÓN — 080_camera_pan_window

<!-- generado por tools/analyze/vision-final.mjs; no editar a mano -->

- Fecha: 2026-10-18T22:25:27.056Z
- Modelo: `qwen3-vl:8b-instruct-q8_0`
- Frames: frame_000.png, frame_005.png, frame_010.png, frame_014.png, frame_019.png (5)
- Secuencia: `out/run/080_camera_pan_window/A500_debug/sequence`

## Prompt (canónico, tools/analyze/vision-prompt.txt)

```text
Las imágenes adjuntas van EN ORDEN TEMPORAL (si hay más de una). Describe con precisión qué ves: todos los elementos gráficos que aparecen, dónde están situados y qué hacen. Indica cualquier elemento que cambie entre imágenes y cualquier cosa que parezca extraña, defectuosa o incoherente (posiciones raras, parpadeos, recortes, artefactos, elementos superpuestos o incompletos). Presta especial atención a los objetos que se mueven: examina su CONTORNO en cada imagen (¿está limpio el perfil del objeto, sin banda o halo rectangular alrededor, sin restos de su posición anterior, sin píxeles cortados o colgando en los bordes?) y el FONDO inmediatamente alrededor (¿queda igual que cuando el objeto no estaba encima, o el objeto lo tapa con un rectángulo de color uniforme?). No adivines el propósito del programa: describe solo lo observable. Responde de forma concisa y ordenada.
```

## Respuesta (cruda)

Las tres imágenes son idénticas. No hay cambios en la composición, los elementos gráficos, ni en los datos de texto mostrados en la parte inferior.

**Descripción de lo observable:**

*   **Elemento gráfico principal:** Una ventana rectangular con bordes pixelados, centrada en la parte superior de la imagen. Dentro de esta ventana se muestra una escena en perspectiva isométrica.
    *   **Fondo de la escena:** Un plano inclinado de color verde oliva en la parte superior y un plano más oscuro, de tono grisáceo, en la parte inferior.
    *   **Objetos en la escena:** Cuatro barras verticales, dispuestas en dos pares.
        *   El par de la izquierda: una barra cian y una barra amarilla, muy cercanas entre sí.
        *   El par de la derecha: una barra cian y una barra naranja, también muy cercanas entre sí.
    *   **Contorno de los objetos:** Los contornos de las barras son limpios, sin halos ni artefactos visibles. No hay píxeles cortados ni colgando en los bordes.
    *   **Fondo alrededor de los objetos:** El fondo inmediato alrededor de las barras es uniforme y consistente con el plano de la escena, sin restos de posiciones anteriores ni artefactos.

*   **Texto en la parte inferior:** Una línea de texto en blanco sobre fondo negro, que permanece exactamente igual en todas las imágenes.
    *   `080 CAMERA PAN WINDOW`
    *   `FULL 12.5 + FAKE 25`
    *   `FRAME 0 FULL 0 FAKE 1`
    *   `FAKE OFF 0 FAC 6 VER 26 PAN -128 K 2`

**Conclusiones:**

*   **Cambio entre imágenes:** No hay ningún cambio entre las tres imágenes. Son exactamente iguales.
*   **Elementos extraños o defectuosos:** No se observan artefactos, parpadeos, recortes, elementos superpuestos o incompletos. La imagen es coherente y completa en todos los aspectos visibles.
*   **Movimiento:** No hay movimiento observable, ya que las imágenes son idénticas.
