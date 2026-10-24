# EVALUACIÓN DE VISIÓN — 091_scene3d_nodes

<!-- generado por tools/analyze/vision-final.mjs; no editar a mano -->

- Fecha: 2026-10-23T23:45:46.113Z
- Modelo: `qwen3-vl:8b-instruct-q8_0`
- Frames: frame_000.png, frame_001.png, frame_002.png, frame_003.png (4)
- Secuencia: `out/run/091_scene3d_nodes/A500_debug/sequence`

## Prompt (canónico, tools/analyze/vision-prompt.txt)

```text
Las imágenes adjuntas van EN ORDEN TEMPORAL (si hay más de una). Describe con precisión qué ves: todos los elementos gráficos que aparecen, dónde están situados y qué hacen. Indica cualquier elemento que cambie entre imágenes y cualquier cosa que parezca extraña, defectuosa o incoherente (posiciones raras, parpadeos, recortes, artefactos, elementos superpuestos o incompletos). Presta especial atención a los objetos que se mueven: examina su CONTORNO en cada imagen (¿está limpio el perfil del objeto, sin banda o halo rectangular alrededor, sin restos de su posición anterior, sin píxeles cortados o colgando en los bordes?) y el FONDO inmediatamente alrededor (¿queda igual que cuando el objeto no estaba encima, o el objeto lo tapa con un rectángulo de color uniforme?). No adivines el propósito del programa: describe solo lo observable. Responde de forma concisa y ordenada.
```

## Respuesta (cruda)

**Imagen 1:**
- **Fondo:** Negro uniforme.
- **Objetos:** Dos cubos tridimensionales representados con líneas blancas discontinuas (punteadas). El cubo superior es más grande y está inclinado hacia la derecha. El cubo inferior es más pequeño y está posicionado debajo del superior, también inclinado.
- **Texto en la esquina superior izquierda:**
  - `091 SCENE3D NODE5 v1.0.6237`
  - `F15 7.9FPS`
  - `E24 J96 OK OK`
- **Estado del contorno:** Los bordes de los cubos son limpios, sin halos ni artefactos. No hay píxeles cortados.
- **Estado del fondo:** El fondo negro permanece uniforme y no muestra restos de la posición anterior de los objetos.

**Imagen 2:**
- **Fondo:** Negro uniforme.
- **Objetos:** Los dos cubos están en una nueva configuración. El cubo superior ahora está más centrado y orientado hacia arriba. El cubo inferior está más cerca del centro y parece estar girando hacia la izquierda.
- **Texto en la esquina superior izquierda:**
  - `091 SCENE3D NODE5 v1.0.6237`
  - `F24 7.0FPS` (cambio de `F15` a `F24`)
  - `E24 J96 OK OK`
- **Estado del contorno:** Los bordes de los cubos son limpios, sin halos ni artefactos. No hay píxeles cortados.
- **Estado del fondo:** El fondo negro permanece uniforme y no muestra restos de la posición anterior de los objetos.

**Cambios entre imágenes:**
- La posición y orientación de los dos cubos han cambiado.
- El texto `F15` ha cambiado a `F24` en la segunda imagen.
- El valor de FPS ha disminuido de 7.9 a 7.0.
