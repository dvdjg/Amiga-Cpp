# EVALUACIÓN DE VISIÓN — 011_typed_memory

<!-- generado por tools/analyze/vision-final.mjs; no editar a mano -->

- Fecha: 2026-10-18T19:43:08.266Z
- Modelo: `qwen3-vl:8b-instruct-q8_0`
- Frames: frame_000.png, frame_002.png, frame_003.png, frame_005.png (4)
- Secuencia: `out/run/011_typed_memory/A500_debug/sequence`

## Prompt (canónico, tools/analyze/vision-prompt.txt)

```text
Las imágenes adjuntas van EN ORDEN TEMPORAL (si hay más de una). Describe con precisión qué ves: todos los elementos gráficos que aparecen, dónde están situados y qué hacen. Indica cualquier elemento que cambie entre imágenes y cualquier cosa que parezca extraña, defectuosa o incoherente (posiciones raras, parpadeos, recortes, artefactos, elementos superpuestos o incompletos). Presta especial atención a los objetos que se mueven: examina su CONTORNO en cada imagen (¿está limpio el perfil del objeto, sin banda o halo rectangular alrededor, sin restos de su posición anterior, sin píxeles cortados o colgando en los bordes?) y el FONDO inmediatamente alrededor (¿queda igual que cuando el objeto no estaba encima, o el objeto lo tapa con un rectángulo de color uniforme?). No adivines el propósito del programa: describe solo lo observable. Responde de forma concisa y ordenada.
```

## Respuesta (cruda)

Las dos imágenes son casi idénticas, mostrando una pantalla de texto en negro con caracteres de colores sobre un fondo negro. El contenido textual es el mismo en ambas, excepto por un valor numérico en la línea inferior.

**Elementos observables:**

*   **Texto superior (en la esquina superior izquierda):**
    *   `Memoria tipada: OK` (en color blanco).
    *   `Planos DMA: Block<PlaneTag,Chip> + MemView` (en color verde).
    *   `Fast RAM: si` (en color amarillo).
    *   `Sim buffer: Fast` (en color cian).
    *   Todos estos elementos están fijos en la esquina superior izquierda y no cambian entre las imágenes.

*   **Texto inferior (en la parte inferior central):**
    *   `TYPED MEM v1.0.4478 F 150` en la primera imagen.
    *   `TYPED MEM v1.0.4478 F 300` en la segunda imagen.
    *   Este texto es el único elemento que cambia. La diferencia es el valor numérico: `150` en la primera imagen y `300` en la segunda. El resto del texto (`TYPED MEM v1.0.4478 F`) es idéntico.

*   **Fondo y contornos:**
    *   El fondo es completamente negro en ambas imágenes.
    *   Los caracteres tienen contornos limpios, sin halos ni artefactos visibles.
    *   No hay píxeles cortados ni restos de posiciones anteriores.
    *   El texto no tapa ningún fondo; el fondo permanece negro y uniforme detrás de los caracteres.

**Cambios entre imágenes:**

*   El único cambio observable es el número `150` que se convierte en `300` en la línea inferior del texto. Todo lo demás permanece exactamente igual.

**Observaciones:**

*   No hay movimiento, animación ni elementos gráficos adicionales.
*   La disposición de los elementos es estática y no cambia.
*   No hay artefactos, recortes ni superposiciones.
*   La apariencia es consistente y limpia en ambas imágenes.
