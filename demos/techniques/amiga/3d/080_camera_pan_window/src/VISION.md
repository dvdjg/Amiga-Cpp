# EVALUACIÓN DE VISIÓN — 080_camera_pan_window

<!-- generado por tools/analyze/vision-final.mjs; no editar a mano -->

- Fecha: 2026-10-20T23:06:16.792Z
- Modelo: `qwen3-vl:8b-instruct-q8_0`
- Frames: frame_000_f0081.png, frame_004_f0085.png, frame_009_f0090.png, frame_013_f0094.png, frame_018_f0099.png, frame_022_f0103.png, frame_027_f0108.png, frame_031_f0112.png (8)
- Secuencia: `out/run/080_camera_pan_window/A500_debug/sequence`

## Prompt (canónico, tools/analyze/vision-prompt.txt)

```text
Las imágenes adjuntas van EN ORDEN TEMPORAL (si hay más de una). Describe con precisión qué ves: todos los elementos gráficos que aparecen, dónde están situados y qué hacen. Indica cualquier elemento que cambie entre imágenes y cualquier cosa que parezca extraña, defectuosa o incoherente (posiciones raras, parpadeos, recortes, artefactos, elementos superpuestos o incompletos). Presta especial atención a los objetos que se mueven: examina su CONTORNO en cada imagen (¿está limpio el perfil del objeto, sin banda o halo rectangular alrededor, sin restos de su posición anterior, sin píxeles cortados o colgando en los bordes?) y el FONDO inmediatamente alrededor (¿queda igual que cuando el objeto no estaba encima, o el objeto lo tapa con un rectángulo de color uniforme?). No adivines el propósito del programa: describe solo lo observable. Responde de forma concisa y ordenada.
```

## Respuesta (cruda)

Las cuatro imágenes muestran una escena gráfica estática, con una única variación entre ellas: el valor del parámetro "PAN" en la línea de texto inferior.

**Descripción general de la escena:**
- **Fondo:** Una pared de color verde oscuro a ambos lados, con un techo de color azul oscuro en la parte superior.
- **Suelo:** Una superficie marrón que se extiende desde la parte inferior de la imagen hacia el fondo.
- **Objetos centrales:** Dos pares de barras verticales, cada par compuesto por dos barras adyacentes. El par de la izquierda es azul cian y amarillo; el par de la derecha es azul cian y naranja. Están situados en el fondo, sobre el suelo.
- **Texto inferior:** Una línea de texto en blanco sobre fondo negro que muestra parámetros de la cámara.

**Cambio entre imágenes:**
- Las imágenes 1 y 2 son idénticas.
- Las imágenes 3 y 4 son idénticas.
- La única diferencia es en el valor del parámetro "PAN" en la línea de texto:
  - En las imágenes 1 y 2: `PAN -19`
  - En las imágenes 3 y 4: `PAN 13`

**Elementos observables:**
- **Contorno de los objetos:** Los contornos de las barras son limpios, sin halos ni artefactos visibles. No hay píxeles cortados ni colgando en los bordes.
- **Fondo alrededor de los objetos:** El fondo permanece uniforme y sin alteraciones alrededor de las barras.
- **Textura y color:** Los colores son sólidos y uniformes en todas las imágenes.
- **Posición de los objetos:** La posición de las barras y el suelo no cambia entre las imágenes.

**Observaciones:**
- No hay parpadeo, recortes ni elementos superpuestos.
- La escena es completamente estática excepto por el cambio en el valor del parámetro "PAN" en la línea de texto.
- El cambio en "PAN" no se refleja en la posición visual de los objetos en la pantalla, lo que sugiere que este parámetro es un valor de configuración o estado interno del programa, no una transformación visual aplicada a la escena.
