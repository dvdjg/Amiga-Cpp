# EVALUACIÓN DE VISIÓN — 011_typed_memory

<!-- generado por tools/analyze/vision-final.mjs; no editar a mano -->

- Fecha: 2026-10-17T18:44:22.641Z
- Modelo: `qwen3-vl:8b-instruct-q8_0`
- Frames: frame_000.png, frame_002.png, frame_004.png, frame_005.png, frame_007.png (5)
- Secuencia: `out/run/011_typed_memory/A500_debug/sequence`

## Prompt (canónico, tools/analyze/vision-prompt.txt)

```text
Las imágenes adjuntas van EN ORDEN TEMPORAL (si hay más de una). Describe con precisión qué ves: todos los elementos gráficos que aparecen, dónde están situados y qué hacen. Indica cualquier elemento que cambie entre imágenes y cualquier cosa que parezca extraña, defectuosa o incoherente (posiciones raras, parpadeos, recortes, artefactos, elementos superpuestos o incompletos). No adivines el propósito del programa: describe solo lo observable. Responde de forma concisa y ordenada.
```

## Respuesta (cruda)

Las tres imágenes muestran una interfaz de texto en una pantalla negra, con texto en colores verde, amarillo y cian. El contenido es idéntico en las tres imágenes, excepto por un cambio en el texto de la línea inferior.

**Elementos observables:**

*   **Texto superior (en verde, amarillo y cian):**
    *   `Memoria tipada: OK` (en verde)
    *   `Planos DMA: Block<PlaneTag,Chip> + MemView` (en verde)
    *   `Fast RAM: si` (en amarillo)
    *   `Sim buffer: Fast` (en cian)
    *   Estos elementos están alineados a la izquierda en la parte superior de la pantalla y permanecen inalterados en todas las imágenes.

*   **Texto inferior (en blanco):**
    *   `TYPED MEM v1.0.4397 F 0` en la primera imagen.
    *   `TYPED MEM v1.0.4397 F 50` en la segunda y tercera imágenes.
    *   Este texto está centrado horizontalmente en la parte inferior de la pantalla.

**Cambio entre imágenes:**

*   El único cambio observable es en el texto de la línea inferior. En la primera imagen, el valor es `F 0`. En la segunda y tercera imágenes, el valor cambia a `F 50`. Las imágenes 2 y 3 son idénticas entre sí.

**Otras observaciones:**

*   No hay parpadeos, recortes, artefactos visibles ni elementos superpuestos.
*   La disposición de los elementos es consistente en todas las imágenes.
*   El texto "F 50" en la segunda y tercera imagen parece ser el único elemento que cambia, lo que sugiere que el programa está mostrando un valor que se actualiza.
