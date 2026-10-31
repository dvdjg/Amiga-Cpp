# EVALUACIÓN DE VISIÓN — 211_plane_bob_teleport

<!-- generado por tools/analyze/vision-final.mjs; no editar a mano -->

- Fecha: 2026-10-31T07:27:19.325Z
- Modelo: `qwen3-vl:8b-instruct-q8_0`
- Frames: frame_000.png, frame_006.png, frame_012.png, frame_017.png, frame_023.png, frame_029.png (6)
- Secuencia: `out/run/211_plane_bob_teleport/A500_release/sequence`

## Prompt (canónico, tools/analyze/vision-prompt.txt)

```text
Las imágenes adjuntas van EN ORDEN TEMPORAL (si hay más de una). Describe con precisión qué ves: todos los elementos gráficos que aparecen, dónde están situados y qué hacen. Indica cualquier elemento que cambie entre imágenes y cualquier cosa que parezca extraña, defectuosa o incoherente (posiciones raras, parpadeos, recortes, artefactos, elementos superpuestos o incompletos). Presta especial atención a los objetos que se mueven: examina su CONTORNO en cada imagen (¿está limpio el perfil del objeto, sin banda o halo rectangular alrededor, sin restos de su posición anterior, sin píxeles cortados o colgando en los bordes?) y el FONDO inmediatamente alrededor (¿queda igual que cuando el objeto no estaba encima, o el objeto lo tapa con un rectángulo de color uniforme?). No adivines el propósito del programa: describe solo lo observable. Responde de forma concisa y ordenada.
```

## Respuesta (cruda)

**Análisis de las imágenes en orden temporal:**

**Imagen 1:**
- **Fondo:** Pantalla de color azul oscuro uniforme.
- **Objetos visibles:**
  - Dos puntos rojos, pequeños y pixelados, ubicados en la parte superior izquierda. El superior está ligeramente más a la izquierda que el inferior.
  - Dos puntos amarillos, pequeños y pixelados, ubicados en la parte inferior derecha. El superior está ligeramente más a la izquierda que el inferior.
  - Un punto gris, pequeño y pixelado, ubicado en la parte central derecha.
- **Texto en la parte superior:** "211 FAST PLANE BOBS v1.0.7652 | F31 68.6FPS | J6 B6 D6".
- **Estado general:** Todos los objetos están completamente visibles, sin artefactos ni píxeles cortados. El fondo es uniforme y no muestra restos de objetos anteriores.

**Imagen 2:**
- **Fondo:** Igual que la imagen 1.
- **Objetos visibles:**
  - Dos puntos rojos, pequeños y pixelados, ahora ubicados en la parte central izquierda. El superior está ligeramente más a la izquierda que el inferior.
  - Dos puntos amarillos, pequeños y pixelados, ubicados en la parte inferior derecha. El superior está ligeramente más a la izquierda que el inferior.
  - Dos puntos grises, pequeños y pixelados, ubicados en la parte central derecha. Uno está ligeramente más arriba que el otro.
- **Texto en la parte superior:** "211 FAST PLANE BOBS v1.0.7652 | F135 49.2FPS | J6 B6 D6".
- **Cambio observable:** Los puntos grises han aumentado de número (de uno a dos). Los puntos rojos han cambiado de posición (de la parte superior izquierda a la central izquierda). Los puntos amarillos y el fondo permanecen sin cambios.

**Imagen 3:**
- **Fondo:** Igual que las imágenes anteriores.
- **Objetos visibles:**
  - Dos puntos rojos, pequeños y pixelados, ubicados en la parte central izquierda. El superior está ligeramente más a la izquierda que el inferior.
  - Dos puntos amarillos, pequeños y pixelados, ubicados en la parte inferior derecha. El superior está ligeramente más a la izquierda que el inferior.
  - Dos puntos grises, pequeños y pixelados, ubicados en la parte central derecha. Uno está ligeramente más arriba que el otro.
- **Texto en la parte superior:** "211 FAST PLANE BOBS v1.0.7652 | F228 49.2FPS | J6 B6 D6".
- **Cambio observable:** Los puntos rojos han cambiado de posición (de la parte central izquierda a la parte inferior izquierda). Los puntos grises y amarillos permanecen en la misma posición que en la imagen 2. El fondo es uniforme y no muestra restos de objetos anteriores.

**Observaciones generales:**
- Todos los objetos son pequeños, pixelados y tienen contornos limpios, sin halos ni píxeles cortados.
- El fondo es uniforme y no muestra artefactos.
- No hay elementos superpuestos ni incompletos.
- Los cambios entre imágenes son consistentes con el movimiento de los objetos, sin artefactos visibles.
