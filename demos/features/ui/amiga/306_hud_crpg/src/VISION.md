# EVALUACIÓN DE VISIÓN — ui_amiga_306_hud_crpg

<!-- generado por tools/analyze/vision-final.mjs; no editar a mano -->

- Fecha: 2026-10-18T20:12:08.254Z
- Modelo: `qwen3-vl:8b-instruct-q8_0`
- Frames: frame_000.png, frame_001.png, frame_003.png, frame_004.png, frame_006.png, frame_007.png (6)
- Secuencia: `out/run/ui_amiga_306_hud_crpg/A500_debug/sequence`

## Prompt (canónico, tools/analyze/vision-prompt.txt)

```text
Las imágenes adjuntas van EN ORDEN TEMPORAL (si hay más de una). Describe con precisión qué ves: todos los elementos gráficos que aparecen, dónde están situados y qué hacen. Indica cualquier elemento que cambie entre imágenes y cualquier cosa que parezca extraña, defectuosa o incoherente (posiciones raras, parpadeos, recortes, artefactos, elementos superpuestos o incompletos). Presta especial atención a los objetos que se mueven: examina su CONTORNO en cada imagen (¿está limpio el perfil del objeto, sin banda o halo rectangular alrededor, sin restos de su posición anterior, sin píxeles cortados o colgando en los bordes?) y el FONDO inmediatamente alrededor (¿queda igual que cuando el objeto no estaba encima, o el objeto lo tapa con un rectángulo de color uniforme?). No adivines el propósito del programa: describe solo lo observable. Responde de forma concisa y ordenada.
```

## Respuesta (cruda)

Las tres imágenes son idénticas entre sí. No hay cambios en el contenido visual, la posición de los elementos ni en la calidad de la representación.

**Descripción de los elementos observables:**

*   **Fondo:** Es completamente negro.
*   **Elementos gráficos en la parte superior:**
    *   En la esquina superior derecha, hay un rectángulo de color azul pálido con dos líneas horizontales dentro, una arriba y otra abajo.
    *   Más abajo, centrado horizontalmente, hay un rectángulo más pequeño de color azul oscuro.
    *   A la izquierda, hay un rectángulo de color azul oscuro.
    *   En el centro, hay un rectángulo vertical de color azul oscuro.
*   **Elementos gráficos en la parte inferior (HUD):**
    *   En la parte inferior izquierda, hay una fila de cinco cuadrados: blanco, amarillo, rojo, blanco, amarillo. El cuadrado blanco de la izquierda tiene un borde rojo.
    *   A la derecha de los cuadrados, hay una barra de texto con las palabras: `LOOK TAKE USE OPEN TALK`. La palabra `TALK` está resaltada con un fondo amarillo y un borde negro.
    *   Debajo de esa barra, se muestra: `V:4 S:--`.
    *   Debajo de eso, se muestra: `VERB:LOOK ITEM:--`.
    *   En la parte inferior más baja, se muestra: `HUD CRPG v1.0.4911 F__00100 FPS 49.90`.

**Análisis de movimientos y artefactos:**

*   **Movimiento:** No hay ningún elemento que se mueva entre las imágenes. Todo está estático.
*   **Contornos y bordes:** Todos los rectángulos tienen bordes limpios y definidos, sin halos, bandas ni píxeles cortados. No hay artefactos visibles.
*   **Fondo alrededor:** El fondo negro permanece uniforme y sin alteraciones alrededor de los elementos gráficos. No hay restos de posiciones anteriores ni superposiciones incompletas.
*   **Coherencia:** La composición es consistente en todas las imágenes. No hay elementos extraños, defectuosos o incoherentes.

En resumen, las tres imágenes son exactamente iguales, mostrando una interfaz de usuario estática de un juego de rol con una HUD visible.
