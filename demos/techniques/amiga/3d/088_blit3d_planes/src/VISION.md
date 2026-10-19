# EVALUACIÓN DE VISIÓN — 088_blit3d_planes

<!-- generado por tools/analyze/vision-final.mjs; no editar a mano -->

- Fecha: 2026-10-19T23:13:14.753Z
- Modelo: `qwen3-vl:8b-instruct-q8_0`
- Frames: frame_000.png, frame_001.png, frame_002.png, frame_003.png (4)
- Secuencia: `out/run/088_blit3d_planes/A500_debug/sequence`

## Prompt (canónico, tools/analyze/vision-prompt.txt)

```text
Las imágenes adjuntas van EN ORDEN TEMPORAL (si hay más de una). Describe con precisión qué ves: todos los elementos gráficos que aparecen, dónde están situados y qué hacen. Indica cualquier elemento que cambie entre imágenes y cualquier cosa que parezca extraña, defectuosa o incoherente (posiciones raras, parpadeos, recortes, artefactos, elementos superpuestos o incompletos). Presta especial atención a los objetos que se mueven: examina su CONTORNO en cada imagen (¿está limpio el perfil del objeto, sin banda o halo rectangular alrededor, sin restos de su posición anterior, sin píxeles cortados o colgando en los bordes?) y el FONDO inmediatamente alrededor (¿queda igual que cuando el objeto no estaba encima, o el objeto lo tapa con un rectángulo de color uniforme?). No adivines el propósito del programa: describe solo lo observable. Responde de forma concisa y ordenada.
```

## Respuesta (cruda)

**Imagen 1:**

*   **Objeto central:** Un objeto tridimensional compuesto por dos planos rectangulares superpuestos. El plano frontal es de color rojo sólido. El plano trasero, parcialmente oculto, es de color azul sólido. Ambos planos están rotados en 3D, mostrando una perspectiva isométrica.
*   **Contorno:** Los bordes del objeto son limpios, sin halos ni artefactos visibles. No hay píxeles cortados ni colgando.
*   **Fondo:** El fondo es completamente negro y uniforme. El objeto lo tapa completamente en su área de ocupación.
*   **Texto en la parte inferior izquierda:**
    *   `088 BLIT3D PLANES`
    *   `v1.0.5456`
*   **Texto en la parte inferior derecha:**
    *   `F52` (en color magenta)

**Imagen 2:**

*   **Objeto central:** El mismo objeto tridimensional, pero ahora rotado. El plano rojo sigue siendo frontal, pero el plano azul ahora está más a la derecha y ligeramente más adelantado, cambiando la apariencia de la superposición.
*   **Contorno:** Los bordes del objeto son limpios, sin halos ni artefactos visibles. No hay píxeles cortados ni colgando.
*   **Fondo:** El fondo es completamente negro y uniforme. El objeto lo tapa completamente en su área de ocupación.
*   **Texto en la parte inferior izquierda:**
    *   `088 BLIT3D PLANES`
    *   `v1.0.5456`
*   **Texto en la parte inferior derecha:**
    *   `F925` (en color magenta)

**Cambio entre imágenes:**

*   El objeto 3D ha cambiado de orientación, lo que se evidencia en la posición relativa de los planos rojo y azul.
*   El texto en la esquina inferior derecha ha cambiado de `F52` a `F925`.
