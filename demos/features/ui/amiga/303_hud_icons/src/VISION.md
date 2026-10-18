# EVALUACIÓN DE VISIÓN — ui_amiga_303_hud_icons

<!-- generado por tools/analyze/vision-final.mjs; no editar a mano -->

- Fecha: 2026-10-18T19:03:25.535Z
- Modelo: `qwen3-vl:8b-instruct-q8_0`
- Frames: frame_000.png, frame_001.png, frame_003.png, frame_004.png, frame_006.png, frame_007.png (6)
- Secuencia: `out/run/ui_amiga_303_hud_icons/A500_debug/sequence`

## Prompt (canónico, tools/analyze/vision-prompt.txt)

```text
Las imágenes adjuntas van EN ORDEN TEMPORAL (si hay más de una). Describe con precisión qué ves: todos los elementos gráficos que aparecen, dónde están situados y qué hacen. Indica cualquier elemento que cambie entre imágenes y cualquier cosa que parezca extraña, defectuosa o incoherente (posiciones raras, parpadeos, recortes, artefactos, elementos superpuestos o incompletos). Presta especial atención a los objetos que se mueven: examina su CONTORNO en cada imagen (¿está limpio el perfil del objeto, sin banda o halo rectangular alrededor, sin restos de su posición anterior, sin píxeles cortados o colgando en los bordes?) y el FONDO inmediatamente alrededor (¿queda igual que cuando el objeto no estaba encima, o el objeto lo tapa con un rectángulo de color uniforme?). No adivines el propósito del programa: describe solo lo observable. Responde de forma concisa y ordenada.
```

## Respuesta (cruda)

**Imagen 1:**
- **Fondo:** Negro.
- **Elementos gráficos:**
  - Tres rectángulos de color púrpura oscuro: uno en la parte superior derecha, otro en la izquierda media, y uno más pequeño en el centro inferior.
  - Una barra horizontal oscura en la parte inferior, sobre la cual se superpone un recuadro blanco.
  - **Recuadro inferior (HUD):** Con borde blanco, contiene:
    - **Iconos de ítems (izquierda):** Una cuadrícula de 3x3 iconos. El icono superior izquierdo es rojo, el inferior derecho es amarillo. El icono inferior izquierdo está resaltado con un contorno rojo.
    - **Texto (derecha):** "ITEMS", "SEL 05 FOC 03" (en amarillo).
    - **Texto inferior (izquierda):** "HUD ICONS v1.0.4861".
    - **Texto inferior (derecha):** "00000 FPS 49.9".

**Imagen 2:**
- **Fondo:** Negro.
- **Elementos gráficos:**
  - Los tres rectángulos púrpura oscuro están en la misma posición que en la imagen 1.
  - La barra horizontal oscura y el recuadro blanco están en la misma posición.
  - **Recuadro inferior (HUD):** Con borde blanco, contiene:
    - **Iconos de ítems (izquierda):** La cuadrícula de 3x3 iconos es idéntica a la de la imagen 1, pero ahora el icono inferior izquierdo está resaltado con un contorno rojo.
    - **Texto (derecha):** "ITEMS", "SEL 01 FOC 03" (en amarillo).
    - **Texto inferior (izquierda):** "HUD ICONS v1.0.4861".
    - **Texto inferior (derecha):** "00200 FPS 41.9".

**Imagen 3:**
- **Fondo:** Negro.
- **Elementos gráficos:**
  - Los tres rectángulos púrpura oscuro están en la misma posición que en la imagen 1.
  - La barra horizontal oscura y el recuadro blanco están en la misma posición.
  - **Recuadro inferior (HUD):** Con borde blanco, contiene:
    - **Iconos de ítems (izquierda):** La cuadrícula de 3x3 iconos es idéntica a la de la imagen 2, con el icono inferior izquierdo resaltado con un contorno rojo.
    - **Texto (derecha):** "ITEMS", "SEL 01 FOC 03" (en amarillo).
    - **Texto inferior (izquierda):** "HUD ICONS v1.0.4861".
    - **Texto inferior (derecha):** "00200 FPS 41.9".

---

**Cambios entre imágenes:**

- **Iconos de ítems:** En la imagen 1, el icono superior izquierdo está resaltado. En las imágenes 2 y 3, el icono inferior izquierdo está resaltado.
- **Texto "SEL":** Cambia de "SEL 05" en la imagen 1 a "SEL 01" en las imágenes 2 y 3.
- **FPS:** Cambia de "49.9" en la imagen 1 a "41.9" en las imágenes 2 y 3.
- **Contenido del HUD:** Las imágenes 2 y 3 son idénticas entre sí.

---

**Observaciones:**

- **Contornos de los objetos:** Los contornos de los rectángulos púrpura oscuro y del recuadro HUD son limpios, sin halos ni artefactos visibles.
- **Fondo:** El fondo negro permanece uniforme en todas las imágenes, sin artefactos ni restos de posiciones anteriores.
- **Elementos superpuestos:** No hay elementos superpuestos ni incompletos.
- **Posiciones raras:** No hay posiciones raras o incoherentes. Los elementos mantienen su posición relativa en todas las imágenes.
- **Artefactos:** No se observan artefactos visibles.
