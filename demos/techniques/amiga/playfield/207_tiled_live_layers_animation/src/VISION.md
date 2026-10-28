# EVALUACIÓN DE VISIÓN — 207_tiled_live_layers_animation

<!-- generado por tools/analyze/vision-final.mjs; no editar a mano -->

- Fecha: 2026-10-28T13:39:14.144Z
- Modelo: `qwen3-vl:8b-instruct-q8_0`
- Frames: frame_000.png, frame_001.png, frame_002.png, frame_003.png (4)
- Secuencia: `out/run/207_tiled_live_layers_animation/A500_release/sequence`

## Prompt (canónico, tools/analyze/vision-prompt.txt)

```text
Las imágenes adjuntas van EN ORDEN TEMPORAL (si hay más de una). Describe con precisión qué ves: todos los elementos gráficos que aparecen, dónde están situados y qué hacen. Indica cualquier elemento que cambie entre imágenes y cualquier cosa que parezca extraña, defectuosa o incoherente (posiciones raras, parpadeos, recortes, artefactos, elementos superpuestos o incompletos). Presta especial atención a los objetos que se mueven: examina su CONTORNO en cada imagen (¿está limpio el perfil del objeto, sin banda o halo rectangular alrededor, sin restos de su posición anterior, sin píxeles cortados o colgando en los bordes?) y el FONDO inmediatamente alrededor (¿queda igual que cuando el objeto no estaba encima, o el objeto lo tapa con un rectángulo de color uniforme?). No adivines el propósito del programa: describe solo lo observable. Responde de forma concisa y ordenada.
```

## Respuesta (cruda)

**Imagen 1:**
- **Estado general:** Pantalla de un editor de mapas de estilo pixel art, con una vista isométrica de un entorno rural.
- **Elementos visibles:**
  - **Fondo:** Un mapa con tierra verde, agua azul, piedras marrones, árboles verdes, un tronco de árbol, un fuego en una hoguera de piedras, un contenedor de madera, un pozo, y una casa de madera en la esquina superior derecha.
  - **Interfaz superior:** Texto que indica "207 TILED LIVE v1.0.7241 | F18 0.0FPS | X=104 Y=368 A=2 G=73".
- **Observaciones:**
  - El FPS (frames per second) es 0.0, lo que indica que el editor está en pausa o no está renderizando el mapa activamente.
  - No hay ningún objeto en movimiento.

**Imagen 2:**
- **Estado general:** La misma escena que en la imagen 1, pero con un cambio en la interfaz superior.
- **Elementos visibles:**
  - **Fondo:** Todo el mapa es idéntico a la imagen 1. No hay cambios en la posición, apariencia o estado de los objetos.
  - **Interfaz superior:** El texto ahora muestra "207 TILED LIVE v1.0.7241 | F24 19.1FPS | X=104 Y=368 A=6 G=73".
- **Observaciones:**
  - El FPS ha aumentado a 19.1, lo que indica que el editor ha comenzado a renderizar el mapa activamente.
  - No hay ningún objeto en movimiento. La escena es estática.
  - **Cambio observable:** El único cambio es el valor del FPS y los valores de A y G en la interfaz superior. El contenido visual del mapa no ha cambiado en absoluto.

**Conclusión:**
- Ambas imágenes muestran exactamente el mismo mapa.
- La única diferencia es el estado del editor: la primera imagen muestra el editor en pausa (0.0 FPS), mientras que la segunda muestra el editor en funcionamiento (19.1 FPS).
- No hay ningún objeto que se mueva, cambie de posición, apariencia o estado entre las dos imágenes. Todo es estático.
