# EVALUACIÓN DE VISIÓN — 500_caverna

<!-- generado por tools/analyze/vision-final.mjs; no editar a mano -->

- Fecha: 2026-10-24T19:08:03.708Z
- Modelo: `qwen3-vl:8b-instruct-q8_0`
- Frames: frame_000.png, frame_001.png, frame_002.png (3)
- Secuencia: `out/run/500_caverna/A500_release/sequence`

## Prompt (canónico, tools/analyze/vision-prompt.txt)

```text
Las imágenes adjuntas van EN ORDEN TEMPORAL (si hay más de una). Describe con precisión qué ves: todos los elementos gráficos que aparecen, dónde están situados y qué hacen. Indica cualquier elemento que cambie entre imágenes y cualquier cosa que parezca extraña, defectuosa o incoherente (posiciones raras, parpadeos, recortes, artefactos, elementos superpuestos o incompletos). Presta especial atención a los objetos que se mueven: examina su CONTORNO en cada imagen (¿está limpio el perfil del objeto, sin banda o halo rectangular alrededor, sin restos de su posición anterior, sin píxeles cortados o colgando en los bordes?) y el FONDO inmediatamente alrededor (¿queda igual que cuando el objeto no estaba encima, o el objeto lo tapa con un rectángulo de color uniforme?). No adivines el propósito del programa: describe solo lo observable. Responde de forma concisa y ordenada.
```

## Respuesta (cruda)

**Imagen 1:**
- **Fondo:** Negro.
- **Elemento fijo inferior:** Un bloque rectangular de caracteres numéricos (2, 3, 4, 5, 6, 7) en color verde claro sobre fondo oscuro, con contorno blanco. Está centrado en la parte inferior de la pantalla.
- **Elementos flotantes superiores:** Seis cuadrados verdes con el número "1" en blanco, distribuidos en la parte superior. Uno está en la esquina superior derecha, otro en la parte superior central, y otros cuatro en posiciones dispersas.
- **Elementos móviles:** Dos rectángulos verticales rojos y dos rectángulos verticales naranjas. Están suspendidos en el aire, sobre el bloque inferior. Uno de los rectángulos naranjas está parcialmente superpuesto sobre el bloque inferior.
- **Texto en la esquina superior izquierda:** "CAVERNA DEMO 500 v1.0.160 | F36 0.0FPS".

**Imagen 2:**
- **Fondo:** Negro.
- **Elemento fijo inferior:** El bloque de caracteres numéricos es idéntico al de la primera imagen.
- **Elementos flotantes superiores:** Los seis cuadrados verdes con "1" están en la misma posición que en la primera imagen.
- **Elementos móviles:**
  - Los dos rectángulos rojos están en la misma posición que en la primera imagen.
  - Los dos rectángulos naranjas han cambiado de posición. El que estaba a la izquierda ha avanzado hacia la derecha, y el que estaba a la derecha ha avanzado hacia la izquierda. Ambos están ahora parcialmente superpuestos sobre el bloque inferior.
- **Texto en la esquina superior izquierda:** "CAVERNA DEMO 500 v1.0.160 | F56 44.5FPS".

**Cambios observables:**
- Los rectángulos naranjas se han movido horizontalmente hacia el centro, superponiéndose parcialmente sobre el bloque inferior.
- El texto en la esquina superior izquierda ha cambiado de "F36 0.0FPS" a "F56 44.5FPS", lo que sugiere que el juego ha comenzado a correr.
- Los demás elementos (cuadrados verdes, rectángulos rojos, bloque inferior) mantienen su posición relativa y apariencia.
