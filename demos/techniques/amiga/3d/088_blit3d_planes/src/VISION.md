# EVALUACIÓN DE VISIÓN — 088_blit3d_planes

<!-- generado por tools/analyze/vision-final.mjs; no editar a mano -->

- Fecha: 2026-10-20T20:20:56.974Z
- Modelo: `qwen3-vl:8b-instruct-q8_0`
- Frames: frame_000.png, frame_004.png, frame_008.png, frame_012.png, frame_016.png, frame_019.png, frame_023.png, frame_027.png, frame_031.png (9)
- Secuencia: `out/run/088_blit3d_planes/A500_debug/sequence`

## Prompt (canónico, tools/analyze/vision-prompt.txt)

```text
Las imágenes adjuntas van EN ORDEN TEMPORAL (si hay más de una). Describe con precisión qué ves: todos los elementos gráficos que aparecen, dónde están situados y qué hacen. Indica cualquier elemento que cambie entre imágenes y cualquier cosa que parezca extraña, defectuosa o incoherente (posiciones raras, parpadeos, recortes, artefactos, elementos superpuestos o incompletos). Presta especial atención a los objetos que se mueven: examina su CONTORNO en cada imagen (¿está limpio el perfil del objeto, sin banda o halo rectangular alrededor, sin restos de su posición anterior, sin píxeles cortados o colgando en los bordes?) y el FONDO inmediatamente alrededor (¿queda igual que cuando el objeto no estaba encima, o el objeto lo tapa con un rectángulo de color uniforme?). No adivines el propósito del programa: describe solo lo observable. Responde de forma concisa y ordenada.
```

## Respuesta (cruda)

**Análisis de las imágenes en orden temporal:**

**Imagen 1:**
- **Objetos visibles:** Dos planos tridimensionales.
  - Un plano grande, de color rojo oscuro, en la parte superior izquierda.
  - Un plano más pequeño, compuesto por dos triángulos (azul y rojo), en la parte superior derecha.
- **Texto en la parte inferior:** "088 BLIT3D PLANES v1.0.5704", "PATH F0028", "B2 AMR1".
- **Estado del fondo:** Negro uniforme, sin artefactos.

**Imagen 2:**
- **Objetos visibles:** Dos planos tridimensionales.
  - Un plano triangular rojo en la esquina superior izquierda.
  - Un plano rectangular de color magenta oscuro, con borde azul, en la parte inferior izquierda.
- **Texto en la parte inferior:** "088 BLIT3D PLANES v1.0.5704", "PATH F0120", "B2 AMR1".
- **Estado del fondo:** Negro uniforme, sin artefactos.

**Imagen 3:**
- **Objetos visibles:** Dos planos tridimensionales.
  - Un plano rectangular azul en la parte superior derecha.
  - Un plano rectangular rojo en la parte inferior derecha.
- **Texto en la parte inferior:** "088 BLIT3D PLANES v1.0.5704", "PATH F0248", "B2 AMR1".
- **Estado del fondo:** Negro uniforme, sin artefactos.

**Imagen 4:**
- **Objetos visibles:** Un solo plano tridimensional.
  - Un plano rectangular de color magenta brillante, en la parte izquierda.
- **Texto en la parte inferior:** "088 BLIT3D PLANES v1.0.5704", "PATH F03A8", "B2 AMR1".
- **Estado del fondo:** Negro uniforme, sin artefactos.

**Cambios observados:**
- El número de planos cambia: de dos en la imagen 1, a dos en la imagen 2, a dos en la imagen 3, y a uno en la imagen 4.
- Los colores y las formas de los planos cambian en cada imagen.
- El valor en "PATH" cambia en cada imagen: F0028 → F0120 → F0248 → F03A8.
- La posición de los planos cambia en cada imagen, moviéndose principalmente hacia la izquierda y hacia abajo.

**Elementos extraños o defectuosos:**
- No se observan artefactos visibles como bandas, halos, píxeles cortados o restos de posiciones anteriores.
- Los contornos de los objetos son limpios y no presentan artefactos.
- El fondo negro permanece uniforme y no muestra rastros de los objetos anteriores.
- No hay superposición de objetos ni elementos incompletos.
