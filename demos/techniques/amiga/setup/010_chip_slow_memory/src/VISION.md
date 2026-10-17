# EVALUACIÓN DE VISIÓN — 010_chip_slow_memory

<!-- generado por tools/analyze/vision-final.mjs; no editar a mano -->

- Fecha: 2026-10-17T21:31:34.770Z
- Modelo: `qwen3-vl:8b-instruct-q8_0`
- Frames: frame_000.png, frame_003.png, frame_006.png, frame_009.png (4)
- Secuencia: `out/run/010_chip_slow_memory/A500_debug/sequence`

## Prompt (canónico, tools/analyze/vision-prompt.txt)

```text
Las imágenes adjuntas van EN ORDEN TEMPORAL (si hay más de una). Describe con precisión qué ves: todos los elementos gráficos que aparecen, dónde están situados y qué hacen. Indica cualquier elemento que cambie entre imágenes y cualquier cosa que parezca extraña, defectuosa o incoherente (posiciones raras, parpadeos, recortes, artefactos, elementos superpuestos o incompletos). No adivines el propósito del programa: describe solo lo observable. Responde de forma concisa y ordenada.
```

## Respuesta (cruda)

Las dos imágenes son idénticas.

**Descripción de los elementos observables:**

*   **Fondo:** Un fondo de color azul oscuro.
*   **Cuadro principal:** Un recuadro de color verde oscuro con borde blanco, centrado en la pantalla.
*   **Texto dentro del cuadro:**
    *   En la parte superior: "AMG demo 010 - Chip/Slow Memory Model".
    *   Debajo: "Profile: A500_IMB_Slow (Slow is capacity, not true Fast)".
    *   Más abajo: "Arena checks: OK" (en amarillo).
    *   Tres barras de progreso horizontales:
        *   La primera, azul, etiquetada "Chip DMA". Está casi llena.
        *   La segunda, verde, etiquetada "Slow metadata". Está a aproximadamente un tercio de su longitud.
        *   La tercera, vacía, etiquetada "Frame scratch".
*   **Texto debajo del cuadro:**
    *   "Chip cap: 0x0000FFF8".
    *   "Slow cap: 0x0007FF8".
*   **Texto en la parte inferior de la pantalla:** "CHIP/SLOW v1.0.4479 F 50".

**Cambios entre imágenes:**

*   **Ninguno.** Las dos imágenes son exactamente iguales en contenido, posición y apariencia.

**Elementos extraños o incoherentes:**

*   **Ninguno observado.** La imagen es coherente y completa. No hay parpadeos, recortes, artefactos ni elementos superpuestos.
