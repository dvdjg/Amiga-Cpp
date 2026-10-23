# EVALUACIÓN DE VISIÓN — 091_scene3d_nodes

<!-- generado por tools/analyze/vision-final.mjs; no editar a mano -->

- Fecha: 2026-10-23T23:23:33.381Z
- Modelo: `qwen3-vl:8b-instruct-q8_0`
- Frames: frame_000.png, frame_001.png, frame_002.png, frame_003.png (4)
- Secuencia: `out/run/091_scene3d_nodes/A500_debug/sequence`

## Prompt (canónico, tools/analyze/vision-prompt.txt)

```text
Las imágenes adjuntas van EN ORDEN TEMPORAL (si hay más de una). Describe con precisión qué ves: todos los elementos gráficos que aparecen, dónde están situados y qué hacen. Indica cualquier elemento que cambie entre imágenes y cualquier cosa que parezca extraña, defectuosa o incoherente (posiciones raras, parpadeos, recortes, artefactos, elementos superpuestos o incompletos). Presta especial atención a los objetos que se mueven: examina su CONTORNO en cada imagen (¿está limpio el perfil del objeto, sin banda o halo rectangular alrededor, sin restos de su posición anterior, sin píxeles cortados o colgando en los bordes?) y el FONDO inmediatamente alrededor (¿queda igual que cuando el objeto no estaba encima, o el objeto lo tapa con un rectángulo de color uniforme?). No adivines el propósito del programa: describe solo lo observable. Responde de forma concisa y ordenada.
```

## Respuesta (cruda)

**Imagen 1:**
- **Fondo:** Pantalla de color azul oscuro uniforme.
- **Elementos gráficos:** Ninguno. La pantalla está completamente vacía.
- **Texto en la esquina superior izquierda:**
  - `091 SCENE3D NODE5 v1.0.6232`
  - `F2 3000FPS` (nota: el valor "3000FPS" es inusual y probablemente un error de visualización o un valor de prueba).
  - `E24 J24 OK OK`

**Imagen 2:**
- **Fondo:** Igual que en la imagen 1, azul oscuro uniforme.
- **Elementos gráficos:**
  - **Objeto principal (superior):** Un rectángulo de contorno naranja, ligeramente inclinado, que parece representar una caja o un objeto tridimensional. Sus bordes son limpios, sin halos ni píxeles cortados.
  - **Objeto secundario (inferior):** Un pequeño rectángulo de contorno naranja, también inclinado, con una línea que se extiende desde su lado derecho hacia arriba y a la derecha, como si fuera una antena o un soporte. Sus bordes también son limpios.
- **Texto en la esquina superior izquierda:**
  - `091 SCENE3D NODE5 v1.0.6232`
  - `F2 13.6FPS` (el valor de FPS ha cambiado de 3000 a 13.6, lo que sugiere que el sistema está renderizando gráficos).
  - `E24 J24 OK OK`

**Cambios entre imágenes:**
- Se ha añadido un objeto gráfico compuesto por dos formas geométricas (un rectángulo grande y uno pequeño con una línea) en la imagen 2.
- El valor de FPS en el texto ha cambiado de "3000FPS" a "13.6FPS".

**Observaciones:**
- Los objetos gráficos tienen contornos limpios y no presentan artefactos visibles.
- El fondo permanece uniforme y no se observan restos de la posición anterior de los objetos.
- No hay elementos superpuestos ni recortes visibles.
