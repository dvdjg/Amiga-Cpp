# EVALUACIÓN DE VISIÓN — 207_tiled_live_layers_animation

<!-- generado por tools/analyze/vision-final.mjs; no editar a mano -->

- Fecha: 2026-10-27T13:02:12.576Z
- Modelo: `qwen3-vl:8b-instruct-q8_0`
- Frames: frame_000.png, frame_013.png, frame_025.png, frame_038.png, frame_051.png, frame_064.png, frame_076.png, frame_089.png (8)
- Secuencia: `out/run/207_tiled_live_layers_animation/A500_release/sequence`

## Prompt (canónico, tools/analyze/vision-prompt.txt)

```text
Las imágenes adjuntas van EN ORDEN TEMPORAL (si hay más de una). Describe con precisión qué ves: todos los elementos gráficos que aparecen, dónde están situados y qué hacen. Indica cualquier elemento que cambie entre imágenes y cualquier cosa que parezca extraña, defectuosa o incoherente (posiciones raras, parpadeos, recortes, artefactos, elementos superpuestos o incompletos). Presta especial atención a los objetos que se mueven: examina su CONTORNO en cada imagen (¿está limpio el perfil del objeto, sin banda o halo rectangular alrededor, sin restos de su posición anterior, sin píxeles cortados o colgando en los bordes?) y el FONDO inmediatamente alrededor (¿queda igual que cuando el objeto no estaba encima, o el objeto lo tapa con un rectángulo de color uniforme?). No adivines el propósito del programa: describe solo lo observable. Responde de forma concisa y ordenada.
```

## Respuesta (cruda)

**Análisis de las imágenes en orden temporal:**

**Imagen 1:**
- **Escena:** Vista isométrica de un entorno pixelado, que parece un pueblo o aldea rural.
- **Elementos visibles:**
  - En la parte superior izquierda, hay una fogata con llamas naranjas y amarillas.
  - A la derecha, una casa con techo de paja y una chimenea.
  - En el centro, un camino de piedras que rodea una zona de césped.
  - Un tronco de árbol (stump) en el centro del camino.
  - Un pequeño charco de agua en la parte inferior izquierda.
  - Árboles y arbustos verdes dispersos.
  - Un contenedor de madera con un líquido azul (posiblemente un pozo o fuente).
  - Un objeto rojo pequeño en el suelo, cerca del centro.
- **Estado:** La escena está completamente estática. No hay ningún elemento en movimiento.

**Imagen 2:**
- **Cambios respecto a la imagen 1:**
  - La cámara se ha desplazado hacia arriba y a la izquierda, mostrando más de la casa de la izquierda y menos de la derecha.
  - El contenedor de madera con líquido azul ahora está más a la izquierda, cerca de la casa.
  - El tronco de árbol se ha movido ligeramente hacia la derecha.
  - El objeto rojo pequeño ha desaparecido.
  - La fogata y el charco de agua están fuera del encuadre.
- **Estado:** La escena sigue siendo estática. No hay movimiento.

**Imagen 3:**
- **Cambios respecto a la imagen 2:**
  - La cámara se ha desplazado hacia arriba y a la derecha.
  - Se ha añadido un nuevo elemento: un poste de madera con una bandera morada en la parte superior derecha.
  - Se ha añadido un árbol grande con forma de pino en la parte superior central.
  - El tronco de árbol se ha movido hacia la izquierda.
  - El contenedor de madera con líquido azul ahora está más a la derecha.
- **Estado:** La escena sigue siendo estática. No hay movimiento.

**Imagen 4:**
- **Cambios respecto a la imagen 3:**
  - La cámara se ha desplazado hacia abajo y a la izquierda.
  - El tronco de árbol se ha movido hacia la izquierda.
  - Se ha añadido un nuevo elemento: una fogata con llamas naranjas y amarillas en la parte superior izquierda.
  - Se ha añadido un nuevo elemento: un objeto rojo pequeño en el suelo, cerca del centro.
  - El contenedor de madera con líquido azul ahora está más a la izquierda.
  - El poste de madera con la bandera morada se ha movido hacia la derecha.
- **Estado:** La escena sigue siendo estática. No hay movimiento.

**Conclusión:**
- Las imágenes muestran una secuencia de desplazamientos de la cámara en un entorno pixelado, sin elementos en movimiento.
- Los elementos gráficos se mueven de posición entre imágenes, pero no cambian de forma ni se superponen.
- No hay artefactos visibles, como bandas, halos o píxeles cortados.
- El fondo permanece intacto en cada imagen, sin evidencia de recortes o recolocaciones.

## Seguimiento de la visita y de las capas vivas

La lectura canónica detecta el desplazamiento de cámara, pero describe la escena como estática: es un falso negativo sobre detalles de 16 px. La secuencia de 90 frames (22,5 s) recorre fogata, agua, flores, casa, esquinas y centro; un recorte 4x de la fogata en la primera parada muestra cómo cambia la silueta de las llamas entre frames consecutivos, y el frame-diff de la secuencia completa no deja pares idénticos durante las paradas. La evidencia válida es la secuencia + frame-diff + recorte, no la frase «no hay movimiento» del modelo. Queda pendiente una pasada por entidad con recortes separados de agua y flores para cerrar la validación visual; no se declara cubierta por el VLM.
