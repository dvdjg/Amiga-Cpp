# EVALUACIÓN DE VISIÓN — 001_adv_walk

<!-- generado por tools/analyze/vision-final.mjs; no editar a mano -->

- Fecha: 2026-10-19T11:57:32.582Z
- Modelo: `qwen3-vl:8b-instruct-q8_0`
- Frames: frame_000.png, frame_002.png, frame_004.png, frame_007.png, frame_009.png, frame_011.png (6)
- Secuencia: `out/run/adventure_amiga_001_adv_walk/A500_debug/sequence`

## Prompt (canónico, tools/analyze/vision-prompt.txt)

```text
Las imágenes adjuntas van EN ORDEN TEMPORAL (si hay más de una). Describe con precisión qué ves: todos los elementos gráficos que aparecen, dónde están situados y qué hacen. Indica cualquier elemento que cambie entre imágenes y cualquier cosa que parezca extraña, defectuosa o incoherente (posiciones raras, parpadeos, recortes, artefactos, elementos superpuestos o incompletos). Presta especial atención a los objetos que se mueven: examina su CONTORNO en cada imagen (¿está limpio el perfil del objeto, sin banda o halo rectangular alrededor, sin restos de su posición anterior, sin píxeles cortados o colgando en los bordes?) y el FONDO inmediatamente alrededor (¿queda igual que cuando el objeto no estaba encima, o el objeto lo tapa con un rectángulo de color uniforme?). No adivines el propósito del programa: describe solo lo observable. Responde de forma concisa y ordenada.
```

## Respuesta (cruda)

**Análisis de las imágenes en orden temporal:**

**Imagen 1:**
*   **Escena:** Vista isométrica de un entorno con suelo de madera, un cuerpo de agua azul con líneas blancas diagonales y un cielo púrpura con tres líneas horizontales blancas.
*   **Personajes:** Dos personajes pixelados de color naranja están presentes. Uno está a la izquierda, cerca del centro de la imagen. El otro está a la derecha, cerca de una estructura con patrón de cuadrícula.
*   **Elementos fijos:** Un objeto rectangular naranja está en el centro del suelo. A la derecha, hay una estructura con patrón de cuadrícula y una forma triangular.
*   **Texto en la parte inferior:** Muestra "001 ADV WALK v1.0.5103", "FRM 29 CAM 0 WP 0/2 BF 0" y "PASEO...".

**Imagen 2:**
*   **Escena:** La escena es idéntica a la imagen 1, con los mismos elementos gráficos y colores.
*   **Cambios observables:**
    *   El personaje de la derecha ha cambiado de posición. Ahora está más cerca de la estructura con patrón de cuadrícula, y su contorno es limpio, sin artefactos.
    *   El personaje de la izquierda ha avanzado ligeramente hacia la derecha.
    *   El texto en la parte inferior ha cambiado: "FRM 29 CAM 0 WP 0/2 BF 0" se ha actualizado a "FRM 234 CAM 2 WP 2/2 BF 0" y aparece "CASEH: .DESTINO (GUIÓN)".
*   **Fondo:** El fondo inmediato alrededor de los personajes y objetos permanece igual, sin artefactos ni restos de posiciones anteriores.

**Imagen 3:**
*   **Escena:** La escena es idéntica a la imagen 2, con los mismos elementos gráficos y colores.
*   **Cambios observables:**
    *   El personaje de la derecha ha cambiado de posición nuevamente, ahora está más cerca de la estructura con patrón de cuadrícula.
    *   El personaje de la izquierda ha avanzado ligeramente más hacia la derecha.
    *   El texto en la parte inferior ha cambiado: "FRM 234 CAM 2 WP 2/2 BF 0" se ha actualizado a "FRM 259 CAM 2 WP 2/2 BF 0" y sigue mostrando "CASEH: .DESTINO (GUIÓN)".
*   **Fondo:** El fondo inmediato alrededor de los personajes y objetos permanece igual, sin artefactos ni restos de posiciones anteriores.

**Resumen de cambios:**
*   **Personajes:** Ambos personajes se mueven hacia la derecha en cada imagen, con movimientos discretos y contornos limpios.
*   **Texto:** El texto en la parte inferior cambia progresivamente, indicando un avance en el juego (FRM aumenta, CAM cambia de 0 a 2, WP cambia de 0/2 a 2/2, y aparece "CASEH: .DESTINO (GUIÓN)").
*   **Fondo:** El fondo permanece constante y sin artefactos en todas las imágenes.
