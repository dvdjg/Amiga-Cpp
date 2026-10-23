# EVALUACIÓN DE VISIÓN — 091_scene3d_nodes

<!-- generado por tools/analyze/vision-final.mjs; no editar a mano -->

- Fecha: 2026-10-23T23:13:20.497Z
- Modelo: `qwen3-vl:8b-instruct-q8_0`
- Frames: frame_000.png, frame_002.png, frame_003.png, frame_005.png (4)
- Secuencia: `out/run/091_scene3d_nodes/A500_debug/sequence`

## Prompt (canónico, tools/analyze/vision-prompt.txt)

```text
Las imágenes adjuntas van EN ORDEN TEMPORAL (si hay más de una). Describe con precisión qué ves: todos los elementos gráficos que aparecen, dónde están situados y qué hacen. Indica cualquier elemento que cambie entre imágenes y cualquier cosa que parezca extraña, defectuosa o incoherente (posiciones raras, parpadeos, recortes, artefactos, elementos superpuestos o incompletos). Presta especial atención a los objetos que se mueven: examina su CONTORNO en cada imagen (¿está limpio el perfil del objeto, sin banda o halo rectangular alrededor, sin restos de su posición anterior, sin píxeles cortados o colgando en los bordes?) y el FONDO inmediatamente alrededor (¿queda igual que cuando el objeto no estaba encima, o el objeto lo tapa con un rectángulo de color uniforme?). No adivines el propósito del programa: describe solo lo observable. Responde de forma concisa y ordenada.
```

## Respuesta (cruda)

**Imagen 1:**
- **Fondo:** Pantalla de color azul oscuro uniforme.
- **Objeto principal:** Un objeto 3D que parece un cubo o un paralelepípedo, representado por un contorno blanco. Está orientado en perspectiva isométrica, con una cara frontal visible y bordes visibles en profundidad.
- **Contorno del objeto:** El contorno es limpio, sin halos ni artefactos. No hay píxeles cortados ni restos de su posición anterior.
- **Fondo alrededor del objeto:** El fondo azul oscuro permanece uniforme y no está tapado por el objeto; el objeto se superpone a él sin dejar rastros de recorte o de color diferente.
- **Texto en la esquina superior izquierda:**
  - `091 SCENE3D NODE5 v1.0.6227`
  - `F1 0.0FPS`

**Imagen 2:**
- **Fondo:** Igual que en la imagen 1, azul oscuro uniforme.
- **Objeto principal:** El mismo objeto 3D, ahora rotado ligeramente en comparación con la imagen 1. La rotación es perceptible en la orientación de sus bordes y caras.
- **Contorno del objeto:** El contorno sigue siendo limpio, sin halos ni artefactos. No hay píxeles cortados ni restos de su posición anterior.
- **Fondo alrededor del objeto:** El fondo azul oscuro permanece uniforme y no está tapado por el objeto; el objeto se superpone a él sin dejar rastros de recorte o de color diferente.
- **Texto en la esquina superior izquierda:**
  - `091 SCENE3D NODE5 v1.0.6227`
  - `F6 3.0FPS`

**Cambios entre imágenes:**
- El objeto 3D ha cambiado de orientación (rotación).
- El valor de FPS en la esquina superior izquierda ha cambiado de `0.0FPS` a `3.0FPS`.

**Observaciones:**
- Ambas imágenes muestran el mismo objeto 3D, pero en diferentes posiciones de rotación.
- El contorno del objeto es siempre limpio y completo en ambos casos.
- El fondo es uniforme y no presenta artefactos ni recortes.
- No hay elementos superpuestos o incompletos.
