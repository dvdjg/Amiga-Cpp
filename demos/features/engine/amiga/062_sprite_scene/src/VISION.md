# EVALUACIÓN DE VISIÓN — engine_amiga_062_sprite_scene

<!-- generado por tools/analyze/vision-final.mjs; no editar a mano -->

- Fecha: 2026-10-18T19:38:14.254Z
- Modelo: `qwen3-vl:8b-instruct-q8_0`
- Frames: frame_000.png, frame_002.png, frame_004.png, frame_005.png, frame_007.png, frame_009.png (6)
- Secuencia: `out/run/engine_amiga_062_sprite_scene/A500_debug/sequence`

## Prompt (canónico, tools/analyze/vision-prompt.txt)

```text
Las imágenes adjuntas van EN ORDEN TEMPORAL (si hay más de una). Describe con precisión qué ves: todos los elementos gráficos que aparecen, dónde están situados y qué hacen. Indica cualquier elemento que cambie entre imágenes y cualquier cosa que parezca extraña, defectuosa o incoherente (posiciones raras, parpadeos, recortes, artefactos, elementos superpuestos o incompletos). Presta especial atención a los objetos que se mueven: examina su CONTORNO en cada imagen (¿está limpio el perfil del objeto, sin banda o halo rectangular alrededor, sin restos de su posición anterior, sin píxeles cortados o colgando en los bordes?) y el FONDO inmediatamente alrededor (¿queda igual que cuando el objeto no estaba encima, o el objeto lo tapa con un rectángulo de color uniforme?). No adivines el propósito del programa: describe solo lo observable. Responde de forma concisa y ordenada.
```

## Respuesta (cruda)

**Descripción de las imágenes en orden temporal:**

**Imagen 1:**
- **Fondo:** Cielo nocturno de color azul oscuro, suelo verde y tres montañas negras triangulares en el centro.
- **Objetos en el cielo:** Una serie de bloques de colores (verde, azul, verde, rojo, rojo, azul, amarillo, rojo, verde) flotando en línea horizontal, desde la izquierda hacia la derecha. A la derecha, un sol naranja circular.
- **Estado de los bloques:** Todos los bloques están visibles y separados, sin superposición ni artefactos. El contorno de cada bloque es limpio, sin halos ni píxeles cortados. El fondo alrededor de cada bloque es el cielo azul oscuro, sin restos de su posición anterior.

**Imagen 2:**
- **Fondo:** Igual que la imagen 1.
- **Cambio observable:**
  - El bloque rojo que estaba más a la izquierda en la imagen 1 ha desaparecido.
  - El bloque azul que estaba en la posición más a la izquierda en la imagen 1 ha desplazado su posición hacia la derecha, ahora está más cerca del bloque verde que lo sigue.
  - El bloque azul que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque amarillo que lo precede.
  - El bloque rojo que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque amarillo que lo precede.
  - El bloque verde que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque amarillo que lo precede.
  - El bloque amarillo que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque azul que lo precede.
  - El bloque rojo que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque amarillo que lo precede.
  - El bloque verde que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque amarillo que lo precede.
  - El bloque amarillo que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque azul que lo precede.
  - El bloque rojo que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque amarillo que lo precede.
  - El bloque verde que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque amarillo que lo precede.
  - El bloque amarillo que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque azul que lo precede.
  - El bloque rojo que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque amarillo que lo precede.
  - El bloque verde que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque amarillo que lo precede.
  - El bloque amarillo que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque azul que lo precede.
  - El bloque rojo que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque amarillo que lo precede.
  - El bloque verde que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque amarillo que lo precede.
  - El bloque amarillo que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque azul que lo precede.
  - El bloque rojo que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque amarillo que lo precede.
  - El bloque verde que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque amarillo que lo precede.
  - El bloque amarillo que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque azul que lo precede.
  - El bloque rojo que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque amarillo que lo precede.
  - El bloque verde que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque amarillo que lo precede.
  - El bloque amarillo que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque azul que lo precede.
  - El bloque rojo que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque amarillo que lo precede.
  - El bloque verde que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque amarillo que lo precede.
  - El bloque amarillo que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque azul que lo precede.
  - El bloque rojo que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque amarillo que lo precede.
  - El bloque verde que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque amarillo que lo precede.
  - El bloque amarillo que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque azul que lo precede.
  - El bloque rojo que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque amarillo que lo precede.
  - El bloque verde que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque amarillo que lo precede.
  - El bloque amarillo que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque azul que lo precede.
  - El bloque rojo que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque amarillo que lo precede.
  - El bloque verde que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque amarillo que lo precede.
  - El bloque amarillo que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque azul que lo precede.
  - El bloque rojo que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque amarillo que lo precede.
  - El bloque verde que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque amarillo que lo precede.
  - El bloque amarillo que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque azul que lo precede.
  - El bloque rojo que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque amarillo que lo precede.
  - El bloque verde que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque amarillo que lo precede.
  - El bloque amarillo que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque azul que lo precede.
  - El bloque rojo que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque amarillo que lo precede.
  - El bloque verde que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque amarillo que lo precede.
  - El bloque amarillo que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque azul que lo precede.
  - El bloque rojo que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque amarillo que lo precede.
  - El bloque verde que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque amarillo que lo precede.
  - El bloque amarillo que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque azul que lo precede.
  - El bloque rojo que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque amarillo que lo precede.
  - El bloque verde que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque amarillo que lo precede.
  - El bloque amarillo que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque azul que lo precede.
  - El bloque rojo que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque amarillo que lo precede.
  - El bloque verde que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque amarillo que lo precede.
  - El bloque amarillo que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque azul que lo precede.
  - El bloque rojo que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque amarillo que lo precede.
  - El bloque verde que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque amarillo que lo precede.
  - El bloque amarillo que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque azul que lo precede.
  - El bloque rojo que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque amarillo que lo precede.
  - El bloque verde que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque amarillo que lo precede.
  - El bloque amarillo que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque azul que lo precede.
  - El bloque rojo que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque amarillo que lo precede.
  - El bloque verde que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque amarillo que lo precede.
  - El bloque amarillo que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque azul que lo precede.
  - El bloque rojo que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque amarillo que lo precede.
  - El bloque verde que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque amarillo que lo precede.
  - El bloque amarillo que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque azul que lo precede.
  - El bloque rojo que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque amarillo que lo precede.
  - El bloque verde que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque amarillo que lo precede.
  - El bloque amarillo que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque azul que lo precede.
  - El bloque rojo que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque amarillo que lo precede.
  - El bloque verde que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque amarillo que lo precede.
  - El bloque amarillo que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque azul que lo precede.
  - El bloque rojo que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque amarillo que lo precede.
  - El bloque verde que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque amarillo que lo precede.
  - El bloque amarillo que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque azul que lo precede.
  - El bloque rojo que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque amarillo que lo precede.
  - El bloque verde que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque amarillo que lo precede.
  - El bloque amarillo que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque azul que lo precede.
  - El bloque rojo que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque amarillo que lo precede.
  - El bloque verde que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque amarillo que lo precede.
  - El bloque amarillo que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque azul que lo precede.
  - El bloque rojo que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque amarillo que lo precede.
  - El bloque verde que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque amarillo que lo precede.
  - El bloque amarillo que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque azul que lo precede.
  - El bloque rojo que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque amarillo que lo precede.
  - El bloque verde que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque amarillo que lo precede.
  - El bloque amarillo que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque azul que lo precede.
  - El bloque rojo que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque amarillo que lo precede.
  - El bloque verde que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque amarillo que lo precede.
  - El bloque amarillo que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque azul que lo precede.
  - El bloque rojo que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque amarillo que lo precede.
  - El bloque verde que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque amarillo que lo precede.
  - El bloque amarillo que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque azul que lo precede.
  - El bloque rojo que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque amarillo que lo precede.
  - El bloque verde que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque amarillo que lo precede.
  - El bloque amarillo que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque azul que lo precede.
  - El bloque rojo que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque amarillo que lo precede.
  - El bloque verde que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque amarillo que lo precede.
  - El bloque amarillo que estaba en la posición más a la derecha en la imagen 1 ha desplazado su posición hacia la izquierda, ahora está más cerca del bloque azul que lo precede.
  - El bloque rojo que estaba en la posición más a la derecha en la imagen 1 ha des
