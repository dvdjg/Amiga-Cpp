# EVALUACIÓN DE VISIÓN — 092_articulated_character

<!-- generado por tools/analyze/vision-final.mjs; no editar a mano -->

- Fecha: 2026-10-24T08:51:58.320Z
- Modelo: `qwen3-vl:8b-instruct-q8_0`
- Frames: frame_000.png, frame_001.png, frame_002.png, frame_003.png (4)
- Secuencia: `out/run/092_articulated_character/A500_release/sequence`

## Prompt (canónico, tools/analyze/vision-prompt.txt)

```text
Las imágenes adjuntas van EN ORDEN TEMPORAL (si hay más de una). Describe con precisión qué ves: todos los elementos gráficos que aparecen, dónde están situados y qué hacen. Indica cualquier elemento que cambie entre imágenes y cualquier cosa que parezca extraña, defectuosa o incoherente (posiciones raras, parpadeos, recortes, artefactos, elementos superpuestos o incompletos). Presta especial atención a los objetos que se mueven: examina su CONTORNO en cada imagen (¿está limpio el perfil del objeto, sin banda o halo rectangular alrededor, sin restos de su posición anterior, sin píxeles cortados o colgando en los bordes?) y el FONDO inmediatamente alrededor (¿queda igual que cuando el objeto no estaba encima, o el objeto lo tapa con un rectángulo de color uniforme?). No adivines el propósito del programa: describe solo lo observable. Responde de forma concisa y ordenada.
```

## Respuesta (cruda)

**Imagen 1:**
- **Fondo:** Negro uniforme.
- **Elementos gráficos:**
  - Un rectángulo naranja, posicionado verticalmente en el centro de la imagen.
  - Un cuadrado azul claro, situado a la derecha del rectángulo naranja, con su borde inferior alineado con el borde superior del rectángulo naranja.
  - Un rectángulo azul claro más largo, inclinado hacia la izquierda y arriba, con su borde inferior alineado con el borde superior del cuadrado azul.
- **Texto en la parte superior izquierda:** "692 ARTICULATED CHARACTER v1.0.6320 | F52010F95P$ | P335B3F3".
- **Estado:** Todos los elementos están completamente visibles, con contornos limpios y sin artefactos. El fondo negro está intacto alrededor de los objetos.

**Imagen 2:**
- **Fondo:** Negro uniforme.
- **Elementos gráficos:**
  - El rectángulo naranja ha cambiado de posición: ahora está completamente desplazado hacia abajo, con su borde inferior más cercano a la parte inferior de la imagen.
  - El cuadrado azul y el rectángulo inclinado azul claro mantienen la misma posición y orientación que en la imagen 1.
- **Texto en la parte superior izquierda:** "692 ARTICULATED CHARACTER v1.0.6320 | F29 18.9FPS | P3 53 F3".
- **Estado:** El rectángulo naranja se ha desplazado verticalmente hacia abajo. Los otros dos elementos permanecen fijos. Los contornos de todos los objetos son limpios y no hay artefactos ni restos de la posición anterior. El fondo negro está intacto alrededor de los objetos.

**Cambios observables:**
- El rectángulo naranja se ha desplazado hacia abajo entre las dos imágenes.
- El texto en la parte superior ha cambiado: la parte "F52010F95P$ | P335B3F3" se ha reemplazado por "F29 18.9FPS | P3 53 F3".
