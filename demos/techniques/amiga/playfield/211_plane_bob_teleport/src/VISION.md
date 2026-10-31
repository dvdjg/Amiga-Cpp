# EVALUACIÓN DE VISIÓN — 211_plane_bob_teleport

<!-- generado por tools/analyze/vision-final.mjs; no editar a mano -->

- Fecha: 2026-10-31T07:12:45.159Z
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
- **Elementos visibles:**
  - Dos objetos rojos, pequeños y de forma irregular (parecidos a cruces o estrellas de 4 puntas), ubicados en la parte superior izquierda. El más alto está cerca del borde superior, el otro está más abajo y a la derecha del primero.
  - Dos objetos amarillos, pequeños y de forma irregular, ubicados en la parte inferior derecha. Uno está más abajo y a la derecha que el otro.
  - Un objeto azul, pequeño y de forma irregular, ubicado en la parte central derecha.
- **Texto en la parte superior:** "211 FAST PLANE BOBS v1.0.7646 | F32 68.6FPS | J6 B6 D6".

**Imagen 2:**
- **Fondo:** Igual que la imagen 1.
- **Cambios respecto a la imagen 1:**
  - Los dos objetos rojos se han movido hacia abajo y a la derecha. El más alto ahora está más centrado en la mitad izquierda, y el otro está más abajo y a la derecha.
  - Los dos objetos amarillos se han movido ligeramente hacia arriba y a la izquierda. El más alto ahora está más centrado en la mitad derecha, y el otro está más abajo y a la derecha.
  - El objeto azul se ha movido hacia arriba y a la izquierda, ahora está más centrado en la parte superior.
- **Texto en la parte superior:** "211 FAST PLANE BOBS v1.0.7646 | F132 49.2FPS | J6 B6 D6".

**Imagen 3:**
- **Fondo:** Igual que las imágenes anteriores.
- **Cambios respecto a la imagen 2:**
  - Los dos objetos rojos se han movido hacia abajo y a la derecha. El más alto ahora está más centrado en la mitad derecha, y el otro está más abajo y a la derecha.
  - Los dos objetos amarillos se han movido ligeramente hacia arriba y a la izquierda. El más alto ahora está más centrado en la mitad derecha, y el otro está más abajo y a la derecha.
  - El objeto azul se ha movido hacia arriba y a la izquierda, ahora está más centrado en la parte superior.
- **Texto en la parte superior:** "211 FAST PLANE BOBS v1.0.7646 | F223 49.2FPS | J6 B6 D6".

**Observaciones:**
- Todos los objetos tienen contornos limpios, sin halos ni artefactos visibles.
- No hay elementos superpuestos ni recortes.
- El fondo permanece uniforme y sin cambios, incluso donde los objetos están.
- El texto en la parte superior cambia ligeramente en cada imagen, indicando un cambio en el estado del programa (por ejemplo, el valor de "F" y "FPS" cambia).
