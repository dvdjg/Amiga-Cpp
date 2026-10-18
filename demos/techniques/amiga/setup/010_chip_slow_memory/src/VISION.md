# EVALUACIÓN DE VISIÓN — 010_chip_slow_memory

<!-- generado por tools/analyze/vision-final.mjs; no editar a mano -->

- Fecha: 2026-10-18T19:42:41.083Z
- Modelo: `qwen3-vl:8b-instruct-q8_0`
- Frames: frame_000.png, frame_003.png, frame_006.png, frame_009.png (4)
- Secuencia: `out/run/010_chip_slow_memory/A500_debug/sequence`

## Prompt (canónico, tools/analyze/vision-prompt.txt)

```text
Las imágenes adjuntas van EN ORDEN TEMPORAL (si hay más de una). Describe con precisión qué ves: todos los elementos gráficos que aparecen, dónde están situados y qué hacen. Indica cualquier elemento que cambie entre imágenes y cualquier cosa que parezca extraña, defectuosa o incoherente (posiciones raras, parpadeos, recortes, artefactos, elementos superpuestos o incompletos). Presta especial atención a los objetos que se mueven: examina su CONTORNO en cada imagen (¿está limpio el perfil del objeto, sin banda o halo rectangular alrededor, sin restos de su posición anterior, sin píxeles cortados o colgando en los bordes?) y el FONDO inmediatamente alrededor (¿queda igual que cuando el objeto no estaba encima, o el objeto lo tapa con un rectángulo de color uniforme?). No adivines el propósito del programa: describe solo lo observable. Responde de forma concisa y ordenada.
```

## Respuesta (cruda)

Las dos imágenes son idénticas.

**Descripción de los elementos gráficos:**

*   **Fondo:** Un fondo de color azul oscuro o negro.
*   **Cuadro principal:** Un recuadro con borde blanco, centrado en la imagen. Dentro de este recuadro:
    *   **Texto superior (blanco):** "AMG demo 010 - Chip/Slow Memory Model".
    *   **Texto medio (verde):** "Profile: A500_IMB_Slow (Slow is capacity, not true Fast)".
    *   **Texto inferior (amarillo):** "Arena checks: OK".
    *   **Gráficos de barras (tres filas):**
        *   **Primera barra (azul):** Llena hasta casi el final, con la etiqueta "Chip DMA" a la derecha.
        *   **Segunda barra (verde):** Llena aproximadamente hasta la mitad, con la etiqueta "Slow metadata" a la derecha.
        *   **Tercera barra (vacía):** Con la etiqueta "Frame scratch" a la derecha.
    *   **Texto inferior del recuadro (blanco):**
        *   "Chip cap: 0x0000FFF8"
        *   "Slow cap: 0x0007FF8"
*   **Texto en la parte inferior de la imagen (blanco):** "CHIP/SLOW v1.0.4479 F 50".

**Análisis de cambios y artefactos:**

*   **Cambios entre imágenes:** No hay ningún cambio. Las dos imágenes son exactamente iguales.
*   **Elementos extraños o defectuosos:** No se observan artefactos, parpadeos, recortes, píxeles cortados, elementos superpuestos ni restos de posiciones anteriores. Todos los elementos tienen contornos limpios y el fondo inmediatamente alrededor de cada elemento es uniforme y no muestra rastros de superposición o transiciones.

**Conclusión:**

Las imágenes no muestran una secuencia temporal, sino que son una copia idéntica de la misma pantalla. No hay movimiento ni cambio alguno observable.
