# EVALUACIÓN DE VISIÓN — 092_articulated_character

<!-- generado por tools/analyze/vision-final.mjs; no editar a mano -->

- Fecha: 2026-10-24T09:09:55.355Z
- Modelo: `qwen3-vl:8b-instruct-q8_0`
- Frames: frame_000.png, frame_001.png, frame_002.png, frame_003.png (4)
- Secuencia: `out/run/092_articulated_character/A500_release/sequence`

## Prompt (canónico, tools/analyze/vision-prompt.txt)

```text
Las imágenes adjuntas van EN ORDEN TEMPORAL (si hay más de una). Describe con precisión qué ves: todos los elementos gráficos que aparecen, dónde están situados y qué hacen. Indica cualquier elemento que cambie entre imágenes y cualquier cosa que parezca extraña, defectuosa o incoherente (posiciones raras, parpadeos, recortes, artefactos, elementos superpuestos o incompletos). Presta especial atención a los objetos que se mueven: examina su CONTORNO en cada imagen (¿está limpio el perfil del objeto, sin banda o halo rectangular alrededor, sin restos de su posición anterior, sin píxeles cortados o colgando en los bordes?) y el FONDO inmediatamente alrededor (¿queda igual que cuando el objeto no estaba encima, o el objeto lo tapa con un rectángulo de color uniforme?). No adivines el propósito del programa: describe solo lo observable. Responde de forma concisa y ordenada.
```

## Respuesta (cruda)

**Imagen 1:**
- **Fondo:** Negro uniforme.
- **Elementos gráficos:**
  - Un rectángulo naranja, posicionado en la parte inferior central de la imagen.
  - Sobre el rectángulo naranja, hay un objeto compuesto de dos partes:
    - Un rectángulo azul claro, orientado horizontalmente, situado en la parte superior del objeto.
    - Un cuadrado azul más oscuro, situado debajo del rectángulo azul claro y ligeramente a su derecha, con una leve inclinación hacia la derecha.
- **Texto en la parte superior:** "692 ARTICULATED CHARACTER v1.0.6337 | F20 18.8FPS | P3 53 F3 A841".

**Imagen 2:**
- **Fondo:** Negro uniforme.
- **Elementos gráficos:**
  - El rectángulo naranja y el cuadrado azul oscuro permanecen en la misma posición que en la imagen 1.
  - El rectángulo azul claro ha cambiado de posición: ahora está inclinado hacia la izquierda y se ha desplazado ligeramente hacia arriba y a la izquierda, manteniendo su orientación horizontal.
- **Texto en la parte superior:** "692 ARTICULATED CHARACTER v1.0.6337 | F20 12.5FPS | P3 53 F3 A870".

**Cambios observables:**
- El rectángulo azul claro se ha movido y rotado ligeramente.
- El texto en la parte superior cambia el valor de "FPS" de 18.8 a 12.5 y el valor de "A" de 841 a 870.
- No hay artefactos visibles como bandas, halos, píxeles cortados o restos de posiciones anteriores. Los contornos de los objetos son limpios y el fondo negro permanece uniforme alrededor de ellos.
