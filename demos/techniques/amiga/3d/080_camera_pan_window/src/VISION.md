# EVALUACIÓN DE VISIÓN — 080_camera_pan_window

<!-- generado por tools/analyze/vision-final.mjs; no editar a mano -->

- Fecha: 2026-10-18T17:16:56.501Z
- Modelo: `qwen3-vl:8b-instruct-q8_0`
- Frames: frame_000.png, frame_002.png, frame_004.png, frame_005.png, frame_007.png (5)
- Secuencia: `out/run/080_camera_pan_window/A500_debug/sequence`

## Prompt (canónico, tools/analyze/vision-prompt.txt)

```text
Las imágenes adjuntas van EN ORDEN TEMPORAL (si hay más de una). Describe con precisión qué ves: todos los elementos gráficos que aparecen, dónde están situados y qué hacen. Indica cualquier elemento que cambie entre imágenes y cualquier cosa que parezca extraña, defectuosa o incoherente (posiciones raras, parpadeos, recortes, artefactos, elementos superpuestos o incompletos). Presta especial atención a los objetos que se mueven: examina su CONTORNO en cada imagen (¿está limpio el perfil del objeto, sin banda o halo rectangular alrededor, sin restos de su posición anterior, sin píxeles cortados o colgando en los bordes?) y el FONDO inmediatamente alrededor (¿queda igual que cuando el objeto no estaba encima, o el objeto lo tapa con un rectángulo de color uniforme?). No adivines el propósito del programa: describe solo lo observable. Responde de forma concisa y ordenada.
```

## Respuesta (cruda)

Las tres imágenes son idénticas entre sí. No hay cambio alguno en la composición visual ni en los datos de texto mostrados.

**Descripción de lo observable:**

*   **Elementos gráficos principales:**
    *   Una ventana rectangular en el centro de la pantalla, con un fondo oscuro azul marino.
    *   Dentro de esta ventana, se muestra una escena tridimensional simplificada, con un suelo gris y paredes de color verde oliva.
    *   En el interior de la escena, hay cuatro barras verticales: dos a la izquierda (una cian y una amarilla) y dos a la derecha (una cian y una naranja).
    *   La escena tiene un efecto de pixelación o malla visible en los planos.

*   **Texto en la parte inferior:**
    *   Una línea de texto en blanco sobre fondo negro que permanece exactamente igual en todas las imágenes.
    *   El texto es: `080 CAMERA PAN WINDOW`, `FULL 12.5 + FAKE 25`, `FRAME 0 FULL 1 FAKE 0`, `FULL OFF 0 FAC 7 VER 28 PAN -128 K 2`.

*   **Estado de los elementos:**
    *   Todos los elementos gráficos y el texto están completamente visibles, sin recortes, sin artefactos, sin parpadeos ni movimientos.
    *   El contorno de los objetos es limpio y no presenta halos ni píxeles cortados.
    *   El fondo inmediato alrededor de los objetos es uniforme y no muestra restos de posiciones anteriores.

**Conclusión:** No hay ninguna variación temporal entre las imágenes. Todas muestran exactamente la misma escena y los mismos datos.
