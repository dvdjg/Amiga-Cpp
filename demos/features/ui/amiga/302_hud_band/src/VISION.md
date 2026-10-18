# EVALUACIÓN DE VISIÓN — ui_amiga_302_hud_band

<!-- generado por tools/analyze/vision-final.mjs; no editar a mano -->

- Fecha: 2026-10-18T08:26:35.600Z
- Modelo: `qwen3-vl:8b-instruct-q8_0`
- Frames: frame_000_f0027.png, frame_002_f0029.png, frame_004_f0031.png, frame_007_f0034.png, frame_009_f0036.png, frame_011_f0038.png (6)
- Secuencia: `out/run/ui_amiga_302_hud_band/A500_debug/sequence`

## Prompt (canónico, tools/analyze/vision-prompt.txt)

```text
Las imágenes adjuntas van EN ORDEN TEMPORAL (si hay más de una). Describe con precisión qué ves: todos los elementos gráficos que aparecen, dónde están situados y qué hacen. Indica cualquier elemento que cambie entre imágenes y cualquier cosa que parezca extraña, defectuosa o incoherente (posiciones raras, parpadeos, recortes, artefactos, elementos superpuestos o incompletos). No adivines el propósito del programa: describe solo lo observable. Responde de forma concisa y ordenada.
```

## Respuesta (cruda)

Las tres imágenes son idénticas entre sí. No hay cambios en el contenido visual, ni en la posición de los elementos, ni en los valores mostrados.

**Descripción de los elementos visibles:**

*   **Fondo:** Negro sólido.
*   **Elementos gráficos en la pantalla:**
    *   En la parte superior izquierda: una estructura formada por un bloque cuadrado oscuro, un bloque rectangular vertical más alto de color azul oscuro, y un bloque cuadrado más pequeño de color azul claro a su derecha.
    *   En la parte superior derecha: dos bloques rectangulares horizontales de color azul oscuro, uno más arriba y otro más abajo.
    *   En el centro inferior: un bloque rectangular oscuro horizontal que parece una plataforma o base.
*   **Interfaz de usuario (HUD) en la parte inferior:**
    *   **Score:** A la izquierda, la palabra "SCORE" en letras blancas, seguida del valor "000001" en amarillo.
    *   **Energy:** Debajo de "SCORE", la palabra "ENERGY" en letras blancas, seguida de una barra de progreso horizontal. La barra está parcialmente llena de amarillo (aproximadamente un 20-30%).
    *   **Lives:** A la derecha, la palabra "LIVES" en letras blancas, seguida de cuatro cuadrados amarillos, indicando cuatro vidas.
    *   **Información técnica:** En la parte inferior, la cadena "HUD BAND v1.0.4638 F 00000 FPS 00.0" en letras blancas.

**Observaciones:**

*   No hay ningún cambio, movimiento, parpadeo o artefacto entre las imágenes.
*   Todos los elementos están fijos en su posición.
*   El estado del juego (puntuación, energía, vidas) es el mismo en todas las imágenes.
*   No hay elementos superpuestos ni recortes. La imagen es coherente y completa.
