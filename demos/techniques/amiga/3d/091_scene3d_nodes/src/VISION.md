# EVALUACIÓN DE VISIÓN — 091_scene3d_nodes

<!-- generado por tools/analyze/vision-final.mjs; no editar a mano -->

- Fecha: 2026-10-24T12:24:35.862Z
- Modelo: `qwen3-vl:8b-instruct-q8_0`
- Frames: frame_000.png, frame_001.png, frame_002.png, frame_003.png, frame_004.png (5)
- Secuencia: `out/run/091_scene3d_nodes/A500_release/sequence`

## Prompt (canónico, tools/analyze/vision-prompt.txt)

```text
Las imágenes adjuntas van EN ORDEN TEMPORAL (si hay más de una). Describe con precisión qué ves: todos los elementos gráficos que aparecen, dónde están situados y qué hacen. Indica cualquier elemento que cambie entre imágenes y cualquier cosa que parezca extraña, defectuosa o incoherente (posiciones raras, parpadeos, recortes, artefactos, elementos superpuestos o incompletos). Presta especial atención a los objetos que se mueven: examina su CONTORNO en cada imagen (¿está limpio el perfil del objeto, sin banda o halo rectangular alrededor, sin restos de su posición anterior, sin píxeles cortados o colgando en los bordes?) y el FONDO inmediatamente alrededor (¿queda igual que cuando el objeto no estaba encima, o el objeto lo tapa con un rectángulo de color uniforme?). No adivines el propósito del programa: describe solo lo observable. Responde de forma concisa y ordenada.
```

## Respuesta (cruda)

**Descripción de las imágenes en orden temporal:**

**Imagen 1:**
- **Objeto:** Un objeto 3D compuesto por múltiples cubos o prismas conectados, representado solo por sus líneas de contorno blancas sobre un fondo negro.
- **Posición:** El objeto está centrado en la pantalla.
- **Estado:** El objeto parece estar en una posición inicial o de reposo.
- **Información de interfaz:** En la parte superior izquierda, se muestra el texto `091 SCENE3D NODES v1.0.6365 | F48 29.8FPS | E16 D`.

**Imagen 2:**
- **Objeto:** El mismo objeto 3D, ahora rotado en un ángulo diferente respecto a la imagen anterior.
- **Posición:** El objeto se ha desplazado ligeramente hacia la izquierda y hacia arriba, y su orientación ha cambiado.
- **Estado:** El objeto está en movimiento, girando en 3D.
- **Información de interfaz:** El texto en la parte superior izquierda ha cambiado a `091 SCENE3D NODES v1.0.6365 | F66 31.7FPS | E18 D`. La frecuencia de fotogramas (FPS) ha aumentado.

**Imagen 3:**
- **Objeto:** El objeto 3D ha girado aún más, mostrando una perspectiva diferente.
- **Posición:** El objeto se ha desplazado hacia la izquierda y hacia abajo, y su orientación es distinta a las anteriores.
- **Estado:** El objeto continúa girando en 3D.
- **Información de interfaz:** El texto en la parte superior izquierda es `091 SCENE3D NODES v1.0.6365 | F91 31.7FPS | E16 D`. La frecuencia de fotogramas es la misma que en la imagen 2, pero el contador de frames (F) ha aumentado.

**Cambios observables:**
- El objeto 3D gira continuamente entre las imágenes, cambiando su orientación y posición.
- El texto en la parte superior izquierda cambia en cada imagen, reflejando el estado del programa (frames, FPS, otros parámetros).
- No hay artefactos visibles como bandas, halos, píxeles cortados o restos de posiciones anteriores. El contorno del objeto es limpio y consistente.
- El fondo es uniformemente negro en todas las imágenes, y el objeto lo tapa completamente sin dejar rastros de color o textura en el fondo.

**Observaciones:**
- El objeto se mueve de forma fluida y coherente, sin saltos ni interrupciones visibles.
- La representación es minimalista, solo mostrando las líneas de contorno del objeto.
- No hay elementos superpuestos ni incompletos.
