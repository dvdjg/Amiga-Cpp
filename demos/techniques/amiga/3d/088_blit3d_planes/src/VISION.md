# EVALUACIÓN DE VISIÓN — 088_blit3d_planes

<!-- generado por tools/analyze/vision-final.mjs; no editar a mano -->

- Fecha: 2026-10-19T23:34:54.361Z
- Modelo: `qwen3-vl:8b-instruct-q8_0`
- Frames: frame_000.png, frame_001.png, frame_002.png, frame_003.png (4)
- Secuencia: `out/run/088_blit3d_planes/A500_debug/sequence`

## Prompt (canónico, tools/analyze/vision-prompt.txt)

```text
Las imágenes adjuntas van EN ORDEN TEMPORAL (si hay más de una). Describe con precisión qué ves: todos los elementos gráficos que aparecen, dónde están situados y qué hacen. Indica cualquier elemento que cambie entre imágenes y cualquier cosa que parezca extraña, defectuosa o incoherente (posiciones raras, parpadeos, recortes, artefactos, elementos superpuestos o incompletos). Presta especial atención a los objetos que se mueven: examina su CONTORNO en cada imagen (¿está limpio el perfil del objeto, sin banda o halo rectangular alrededor, sin restos de su posición anterior, sin píxeles cortados o colgando en los bordes?) y el FONDO inmediatamente alrededor (¿queda igual que cuando el objeto no estaba encima, o el objeto lo tapa con un rectángulo de color uniforme?). No adivines el propósito del programa: describe solo lo observable. Responde de forma concisa y ordenada.
```

## Respuesta (cruda)

**Imagen 1:**

*   **Objeto principal:** Un polígono de color rojo intenso, con bordes angulosos y una forma que sugiere un hexágono o un polígono de múltiples lados, orientado en diagonal. A su izquierda, se observa una porción de un polígono azul, que parece estar superpuesto parcialmente sobre el rojo.
*   **Fondo:** Negro sólido.
*   **Texto en la parte inferior izquierda:**
    *   `088 BLIT3D PLANES`
    *   `v1.0.5465`
*   **Texto en la parte inferior derecha:**
    *   `F93`
*   **Estado del objeto:** El contorno del polígono rojo es limpio, sin halos ni artefactos. El fondo negro está intacto alrededor del objeto. No hay píxeles cortados ni restos de una posición anterior.

**Imagen 2:**

*   **Objeto principal:** El mismo polígono rojo, ahora rotado ligeramente hacia la izquierda. La porción azul que lo acompaña también ha cambiado de posición y forma, ahora ocupando un espacio más a la derecha del rojo, con una apariencia más rectangular y extendida.
*   **Fondo:** Negro sólido.
*   **Texto en la parte inferior izquierda:**
    *   `088 BLIT3D PLANES`
    *   `v1.0.5465`
*   **Texto en la parte inferior derecha:**
    *   `F94`
*   **Estado del objeto:** El contorno del polígono rojo es limpio. El fondo negro está intacto alrededor del objeto. No hay píxeles cortados ni restos de una posición anterior.

**Cambios entre imágenes:**

*   El polígono rojo ha girado ligeramente.
*   La forma y posición del polígono azul han cambiado.
*   El texto en la esquina inferior derecha ha cambiado de `F93` a `F94`.
