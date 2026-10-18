# EVALUACIÓN DE VISIÓN — 217_sprite_template_actor

<!-- generado por tools/analyze/vision-final.mjs; no editar a mano -->

- Fecha: 2026-10-18T19:55:09.479Z
- Modelo: `qwen3-vl:8b-instruct-q8_0`
- Frames: frame_000.png, frame_002.png, frame_003.png, frame_005.png (4)
- Secuencia: `out/run/217_sprite_template_actor/A500_debug/sequence`

## Prompt (canónico, tools/analyze/vision-prompt.txt)

```text
Las imágenes adjuntas van EN ORDEN TEMPORAL (si hay más de una). Describe con precisión qué ves: todos los elementos gráficos que aparecen, dónde están situados y qué hacen. Indica cualquier elemento que cambie entre imágenes y cualquier cosa que parezca extraña, defectuosa o incoherente (posiciones raras, parpadeos, recortes, artefactos, elementos superpuestos o incompletos). Presta especial atención a los objetos que se mueven: examina su CONTORNO en cada imagen (¿está limpio el perfil del objeto, sin banda o halo rectangular alrededor, sin restos de su posición anterior, sin píxeles cortados o colgando en los bordes?) y el FONDO inmediatamente alrededor (¿queda igual que cuando el objeto no estaba encima, o el objeto lo tapa con un rectángulo de color uniforme?). No adivines el propósito del programa: describe solo lo observable. Responde de forma concisa y ordenada.
```

## Respuesta (cruda)

**Imagen 1:**

- **Fondo:** Un fondo degradado de azul oscuro a negro, con 7 franjas horizontales de gris, de más oscuro a más claro hacia abajo.
- **Elemento principal:** Un gráfico de velas (candlestick) de color naranja y verde, con líneas rojas y cian.
  - **Posición:** A la izquierda del gráfico.
  - **Descripción:** La vela tiene un cuerpo verde en la parte inferior y un cuerpo naranja en la parte superior. La línea roja (alta) está en la parte superior. La línea cian (baja) está en la parte inferior. El cuerpo verde es más largo que el naranja, lo que sugiere un movimiento alcista.
- **Estado:** El gráfico está completo y sin artefactos visibles.

**Imagen 2:**

- **Fondo:** Igual que en la imagen 1.
- **Elementos principales:** Dos gráficos de velas.
  - **Vela izquierda:** Igual que en la imagen 1, en la misma posición.
  - **Vela derecha:** Nueva vela, idéntica en forma y color a la vela izquierda, pero desplazada hacia la derecha.
    - **Posición:** A la derecha del gráfico.
    - **Descripción:** Cuerpo verde en la parte inferior, cuerpo naranja en la parte superior, línea roja en la parte superior y línea cian en la parte inferior.
- **Estado:** Ambas velas están completas y sin artefactos visibles.

**Cambios entre imágenes:**

- La imagen 2 añade una segunda vela a la derecha de la primera.
- No hay elementos superpuestos ni recortes.
- Los contornos de las velas son limpios, sin halos ni píxeles cortados.
- El fondo permanece igual en ambas imágenes, sin cambios visibles.
