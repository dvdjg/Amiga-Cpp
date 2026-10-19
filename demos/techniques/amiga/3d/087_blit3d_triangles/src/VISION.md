# EVALUACIÓN DE VISIÓN — 087_blit3d_triangles

<!-- generado por tools/analyze/vision-final.mjs; no editar a mano -->

- Fecha: 2026-10-19T17:44:39.096Z
- Modelo: `qwen3-vl:8b-instruct-q8_0`
- Frames: frame_000.png, frame_001.png, frame_002.png, frame_003.png, frame_004.png (5)
- Secuencia: `out/run/087_blit3d_triangles/A500_debug/sequence`

## Prompt (canónico, tools/analyze/vision-prompt.txt)

```text
Las imágenes adjuntas van EN ORDEN TEMPORAL (si hay más de una). Describe con precisión qué ves: todos los elementos gráficos que aparecen, dónde están situados y qué hacen. Indica cualquier elemento que cambie entre imágenes y cualquier cosa que parezca extraña, defectuosa o incoherente (posiciones raras, parpadeos, recortes, artefactos, elementos superpuestos o incompletos). Presta especial atención a los objetos que se mueven: examina su CONTORNO en cada imagen (¿está limpio el perfil del objeto, sin banda o halo rectangular alrededor, sin restos de su posición anterior, sin píxeles cortados o colgando en los bordes?) y el FONDO inmediatamente alrededor (¿queda igual que cuando el objeto no estaba encima, o el objeto lo tapa con un rectángulo de color uniforme?). No adivines el propósito del programa: describe solo lo observable. Responde de forma concisa y ordenada.
```

## Respuesta (cruda)

**Imagen 1:**
- **Objetos:** Cuatro triángulos. Tres son de color verde, uno es de color cian.
- **Posición y orientación:**
  - Triángulo verde: en la parte superior izquierda, orientado hacia abajo.
  - Triángulo verde: en el centro, orientado hacia abajo.
  - Triángulo cian: en la parte inferior derecha, orientado hacia arriba.
  - Triángulo verde: en la parte inferior derecha, orientado hacia arriba.
- **Fondo:** Negro uniforme.
- **Texto en la parte inferior izquierda:**
  - "087 BLIT3D TRIANGLES"
  - "v1.0.5287"
- **Texto en la parte inferior derecha:**
  - "F26"
- **Estado visual:** Todos los triángulos están completamente visibles, sin artefactos ni píxeles cortados. El fondo negro está intacto alrededor de los triángulos.

**Imagen 2:**
- **Objetos:** Cuatro triángulos. Tres son de color cian, uno es de color verde.
- **Posición y orientación:**
  - Triángulo cian: en la parte superior izquierda, orientado hacia abajo.
  - Triángulo cian: en el centro, orientado hacia abajo.
  - Triángulo verde: en la parte inferior derecha, orientado hacia arriba.
- **Fondo:** Negro uniforme.
- **Texto en la parte inferior izquierda:**
  - "087 BLIT3D TRIANGLES"
  - "v1.0.5287"
- **Texto en la parte inferior derecha:**
  - "F37"
- **Estado visual:** Todos los triángulos están completamente visibles, sin artefactos ni píxeles cortados. El fondo negro está intacto alrededor de los triángulos.

**Imagen 3:**
- **Objetos:** Dos triángulos. Uno es de color cian, otro es de color verde.
- **Posición y orientación:**
  - Triángulo cian: en la parte superior central, orientado hacia abajo.
  - Triángulo verde: en la parte inferior derecha, orientado hacia arriba.
- **Fondo:** Negro uniforme.
- **Texto en la parte inferior izquierda:**
  - "087 BLIT3D TRIANGLES"
  - "v1.0.5287"
- **Texto en la parte inferior derecha:**
  - "F64"
- **Estado visual:** Todos los triángulos están completamente visibles, sin artefactos ni píxeles cortados. El fondo negro está intacto alrededor de los triángulos.

**Cambios entre imágenes:**
- La cantidad y el color de los triángulos cambian.
- La posición y orientación de los triángulos cambian.
- El texto "F26" cambia a "F37" y luego a "F64", lo que sugiere un cambio en un parámetro del programa.
- El fondo permanece negro en todas las imágenes.
