# EVALUACIÓN DE VISIÓN — 092_articulated_character

<!-- generado por tools/analyze/vision-final.mjs; no editar a mano -->

- Fecha: 2026-10-24T09:03:07.379Z
- Modelo: `qwen3-vl:8b-instruct-q8_0`
- Frames: frame_000.png, frame_001.png, frame_002.png, frame_003.png (4)
- Secuencia: `out/run/092_articulated_character/A500_release/sequence`

## Prompt (canónico, tools/analyze/vision-prompt.txt)

```text
Las imágenes adjuntas van EN ORDEN TEMPORAL (si hay más de una). Describe con precisión qué ves: todos los elementos gráficos que aparecen, dónde están situados y qué hacen. Indica cualquier elemento que cambie entre imágenes y cualquier cosa que parezca extraña, defectuosa o incoherente (posiciones raras, parpadeos, recortes, artefactos, elementos superpuestos o incompletos). Presta especial atención a los objetos que se mueven: examina su CONTORNO en cada imagen (¿está limpio el perfil del objeto, sin banda o halo rectangular alrededor, sin restos de su posición anterior, sin píxeles cortados o colgando en los bordes?) y el FONDO inmediatamente alrededor (¿queda igual que cuando el objeto no estaba encima, o el objeto lo tapa con un rectángulo de color uniforme?). No adivines el propósito del programa: describe solo lo observable. Responde de forma concisa y ordenada.
```

## Respuesta (cruda)

**Imagen 1:**
- **Elementos gráficos:** Un conjunto de tres formas geométricas sobre fondo negro.
  - Un rectángulo naranja, posicionado en la parte inferior central.
  - Un cuadrado azul brillante, situado encima y ligeramente a la derecha del rectángulo naranja.
  - Un rectángulo azul claro, inclinado hacia la izquierda, que se superpone parcialmente al cuadrado azul brillante.
- **Estado:** Las formas están estacionarias. No hay movimiento ni cambio de posición.
- **Texto en la parte superior izquierda:** `692 ARTICULATED CHARACTER v1.0.6333 | F6 3010 FPS | P3 3583F3`.
- **Fondo:** Uniformemente negro. Las formas tienen contornos limpios, sin halos ni artefactos visibles. El fondo inmediatamente alrededor de las formas es negro y uniforme.

**Imagen 2:**
- **Elementos gráficos:** El mismo conjunto de tres formas geométricas, en la misma posición y orientación que en la imagen 1.
- **Estado:** Las formas están estacionarias. No hay movimiento ni cambio de posición.
- **Texto en la parte superior izquierda:** `692 ARTICULATED CHARACTER v1.0.6333 | F20 11.2FPS | P3 53 F3`.
- **Fondo:** Uniformemente negro. Las formas tienen contornos limpios, sin halos ni artefactos visibles. El fondo inmediatamente alrededor de las formas es negro y uniforme.

**Cambios observables:**
- El texto en la parte superior derecha ha cambiado: `F6 3010 FPS` en la imagen 1 se ha convertido en `F20 11.2FPS` en la imagen 2, y `P3 3583F3` se ha convertido en `P3 53 F3`.
- Las formas gráficas y su posición no han cambiado entre ambas imágenes.

**Observaciones:**
- Ambas imágenes muestran el mismo estado visual de las formas gráficas.
- No hay parpadeos, recortes, artefactos ni elementos superpuestos.
- El fondo es uniformemente negro en ambos casos.
- Las formas tienen contornos limpios y no presentan píxeles cortados o colgando en los bordes.
