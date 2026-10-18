# EVALUACIÓN DE VISIÓN — ui_amiga_302_hud_band

<!-- generado por tools/analyze/vision-final.mjs; no editar a mano -->

- Fecha: 2026-10-18T13:41:09.663Z
- Modelo: `qwen3-vl:8b-instruct-q8_0`
- Frames: frame_000_f0042.png, frame_002_f0044.png, frame_004_f0046.png, frame_007_f0049.png, frame_009_f0051.png, frame_011_f0053.png (6)
- Secuencia: `out/run/ui_amiga_302_hud_band/A500_debug/sequence`

## Prompt (canónico, tools/analyze/vision-prompt.txt)

```text
Las imágenes adjuntas van EN ORDEN TEMPORAL (si hay más de una). Describe con precisión qué ves: todos los elementos gráficos que aparecen, dónde están situados y qué hacen. Indica cualquier elemento que cambie entre imágenes y cualquier cosa que parezca extraña, defectuosa o incoherente (posiciones raras, parpadeos, recortes, artefactos, elementos superpuestos o incompletos). Presta especial atención a los objetos que se mueven: examina su CONTORNO en cada imagen (¿está limpio el perfil del objeto, sin banda o halo rectangular alrededor, sin restos de su posición anterior, sin píxeles cortados o colgando en los bordes?) y el FONDO inmediatamente alrededor (¿queda igual que cuando el objeto no estaba encima, o el objeto lo tapa con un rectángulo de color uniforme?). No adivines el propósito del programa: describe solo lo observable. Responde de forma concisa y ordenada.
```

## Respuesta (cruda)

**Descripción de las imágenes (en orden temporal):**

**Elementos gráficos fijos:**
*   **Fondo:** Un fondo de capas horizontales en tonos de azul oscuro y verde oliva.
*   **Objetos estáticos:** Dos estructuras piramidales invertidas (una verde oscuro, otra marrón) con bloques rojos en su base y en la parte superior. Un bloque rojo horizontal en el suelo entre ellas. Un objeto circular verde brillante en la parte superior izquierda.
*   **HUD inferior:** Una barra negra con texto y barras de estado.
    *   `SCORE 000002`
    *   `ENERGY` con una barra de progreso.
    *   `LIVES` con cuatro cuadrados amarillos.
    *   `HUD BAND v1.0.4734 F 00000 FPS 40.6`

**Elementos gráficos móviles:**
*   **Objeto rosa brillante (en la parte inferior izquierda):** Un objeto rosa con un contorno blanco, que se mueve hacia la derecha.
*   **Objeto rojo (en la parte inferior derecha):** Un objeto rojo con forma de corazón o estrella, que permanece en la misma posición.

**Cambios entre imágenes:**

*   **Imagen 1:** El objeto rosa está en la posición más a la izquierda.
*   **Imagen 2:** El objeto rosa se ha desplazado hacia la derecha, aproximadamente un tercio de su trayectoria.
*   **Imagen 3:** El objeto rosa se ha desplazado aún más hacia la derecha, casi completando su movimiento.

**Observaciones:**

*   **Contorno del objeto rosa:** En todas las imágenes, el objeto rosa tiene un contorno blanco limpio y definido. No hay bandas, halos ni píxeles cortados. El fondo inmediatamente alrededor del objeto es el mismo color que el fondo, sin artefactos visibles.
*   **Estado de la energía:** En la imagen 1, la barra de energía está en amarillo. En la imagen 2, la barra de energía se ha vuelto roja, indicando una pérdida de energía. En la imagen 3, la barra de energía permanece roja.
*   **Estado de las vidas:** En la imagen 1, todas las vidas son amarillas. En la imagen 3, la última vida (la cuarta desde la izquierda) se ha vuelto roja, indicando una pérdida de vida.
*   **Posición del objeto rojo:** El objeto rojo permanece en la misma posición en todas las imágenes.
*   **Posición del objeto verde:** El objeto verde permanece en la misma posición en todas las imágenes.
*   **FPS y Score:** El FPS (40.6) y el score (000002) permanecen constantes en todas las imágenes.
