# EVALUACIÓN DE VISIÓN — 088_blit3d_planes

<!-- generado por tools/analyze/vision-final.mjs; no editar a mano -->

- Fecha: 2026-10-19T20:51:08.135Z
- Modelo: `qwen3-vl:8b-instruct-q8_0`
- Frames: frame_000.png, frame_001.png, frame_002.png, frame_003.png (4)
- Secuencia: `out/run/088_blit3d_planes/A500_debug/sequence`

## Prompt (canónico, tools/analyze/vision-prompt.txt)

```text
Las imágenes adjuntas van EN ORDEN TEMPORAL (si hay más de una). Describe con precisión qué ves: todos los elementos gráficos que aparecen, dónde están situados y qué hacen. Indica cualquier elemento que cambie entre imágenes y cualquier cosa que parezca extraña, defectuosa o incoherente (posiciones raras, parpadeos, recortes, artefactos, elementos superpuestos o incompletos). Presta especial atención a los objetos que se mueven: examina su CONTORNO en cada imagen (¿está limpio el perfil del objeto, sin banda o halo rectangular alrededor, sin restos de su posición anterior, sin píxeles cortados o colgando en los bordes?) y el FONDO inmediatamente alrededor (¿queda igual que cuando el objeto no estaba encima, o el objeto lo tapa con un rectángulo de color uniforme?). No adivines el propósito del programa: describe solo lo observable. Responde de forma concisa y ordenada.
```

## Respuesta (cruda)

**Imagen 1:**

*   **Objeto central:** Un objeto tridimensional compuesto por dos planos principales: uno rojo, que ocupa la mayor parte del volumen visible, y otro azul, que se superpone parcialmente al rojo en su parte inferior izquierda. El objeto tiene un contorno limpio, sin halos ni píxeles cortados. El fondo negro está completamente tapado por el objeto.
*   **Texto en la parte inferior izquierda:**
    *   `088 BLIT3D PLANES`
    *   `v1.0.5398`
*   **Texto en la parte inferior derecha:** `F30`

**Imagen 2:**

*   **Objeto central:** El mismo objeto tridimensional, pero rotado. El plano rojo sigue siendo el más prominente, pero ahora el plano azul se encuentra en la parte superior derecha, superpuesto al rojo. El contorno del objeto es limpio y no presenta artefactos. El fondo negro está completamente tapado por el objeto.
*   **Texto en la parte inferior izquierda:** Igual que en la imagen 1.
*   **Texto en la parte inferior derecha:** `F969`

**Cambios entre imágenes:**

*   El objeto 3D ha cambiado de orientación (rotación).
*   El texto en la esquina inferior derecha ha cambiado de `F30` a `F969`.
