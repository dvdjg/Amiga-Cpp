# EVALUACIÓN DE VISIÓN — 092_articulated_character

<!-- generado por tools/analyze/vision-final.mjs; no editar a mano -->

- Fecha: 2026-10-24T13:04:51.370Z
- Modelo: `qwen3-vl:8b-instruct-q8_0`
- Frames: frame_000.png, frame_001.png, frame_002.png, frame_003.png (4)
- Secuencia: `out/run/092_articulated_character/A500_release/sequence`

## Prompt (canónico, tools/analyze/vision-prompt.txt)

```text
Las imágenes adjuntas van EN ORDEN TEMPORAL (si hay más de una). Describe con precisión qué ves: todos los elementos gráficos que aparecen, dónde están situados y qué hacen. Indica cualquier elemento que cambie entre imágenes y cualquier cosa que parezca extraña, defectuosa o incoherente (posiciones raras, parpadeos, recortes, artefactos, elementos superpuestos o incompletos). Presta especial atención a los objetos que se mueven: examina su CONTORNO en cada imagen (¿está limpio el perfil del objeto, sin banda o halo rectangular alrededor, sin restos de su posición anterior, sin píxeles cortados o colgando en los bordes?) y el FONDO inmediatamente alrededor (¿queda igual que cuando el objeto no estaba encima, o el objeto lo tapa con un rectángulo de color uniforme?). No adivines el propósito del programa: describe solo lo observable. Responde de forma concisa y ordenada.
```

## Respuesta (cruda)

**Imagen 1:**
- **Elementos gráficos:** Un conjunto de tres formas geométricas sobre un fondo negro.
    - Una forma rectangular naranja, ubicada en la parte inferior central.
    - Una forma cuadrada azul brillante, situada encima del rectángulo naranja, ligeramente desplazada hacia la derecha.
    - Una forma rectangular azul más pálida, situada encima de la forma azul brillante, ligeramente desplazada hacia la izquierda.
- **Estado:** Las formas están en una posición estática, con bordes limpios y sin artefactos visibles. El fondo negro está completamente visible alrededor de los objetos.
- **Texto en la parte superior:** `092 ARTICULATED CHARACTER v1.0.6375 | F26 11.1FPS | P3 S3 F3 A314`

**Imagen 2:**
- **Elementos gráficos:** El mismo conjunto de tres formas geométricas, en la misma posición y orientación que en la imagen 1.
- **Estado:** Las formas son idénticas en apariencia y posición a la imagen anterior. No hay movimiento ni cambio de contorno.
- **Texto en la parte superior:** `092 ARTICULATED CHARACTER v1.0.6375 | F30 12.7FPS | P3 S3 F3 A002`

**Cambios observados:**
- **No hay cambios en la posición, forma ni apariencia de los objetos gráficos** entre las dos imágenes.
- **El texto en la parte superior cambia solo en el valor de FPS (de 11.1 a 12.7) y en el valor de A (de A314 a A002)**, lo que sugiere que las imágenes fueron capturadas en momentos distintos, pero el contenido visual del personaje no ha cambiado.

**Observaciones:**
- Los contornos de las formas son limpios, sin halos ni artefactos.
- El fondo negro es uniforme y no muestra ninguna textura o ruido.
- No hay elementos superpuestos ni recortes visibles.
- La apariencia de los objetos es consistente en ambas imágenes.
