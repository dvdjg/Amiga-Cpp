# EVALUACIÓN DE VISIÓN — ui_amiga_304_hud_drag

<!-- generado por tools/analyze/vision-final.mjs; no editar a mano -->

- Fecha: 2026-10-18T20:38:42.809Z
- Modelo: `qwen3-vl:8b-instruct-q8_0`
- Frames: frame_000.png, frame_002.png, frame_004.png, frame_005.png, frame_007.png (5)
- Secuencia: `out/run/ui_amiga_304_hud_drag/A500_debug/sequence`

## Prompt (canónico, tools/analyze/vision-prompt.txt)

```text
Las imágenes adjuntas van EN ORDEN TEMPORAL (si hay más de una). Describe con precisión qué ves: todos los elementos gráficos que aparecen, dónde están situados y qué hacen. Indica cualquier elemento que cambie entre imágenes y cualquier cosa que parezca extraña, defectuosa o incoherente (posiciones raras, parpadeos, recortes, artefactos, elementos superpuestos o incompletos). Presta especial atención a los objetos que se mueven: examina su CONTORNO en cada imagen (¿está limpio el perfil del objeto, sin banda o halo rectangular alrededor, sin restos de su posición anterior, sin píxeles cortados o colgando en los bordes?) y el FONDO inmediatamente alrededor (¿queda igual que cuando el objeto no estaba encima, o el objeto lo tapa con un rectángulo de color uniforme?). No adivines el propósito del programa: describe solo lo observable. Responde de forma concisa y ordenada.
```

## Respuesta (cruda)

**Análisis de las imágenes (en orden temporal):**

**Elementos fijos:**
*   **Fondo:** Negro uniforme.
*   **Objetos gráficos fijos:**
    *   Dos rectángulos de color azul oscuro, uno en la parte superior derecha y otro en la parte inferior izquierda.
    *   Una barra horizontal oscura en la parte inferior de la pantalla.
*   **Interfaz de usuario (HUD) en la barra inferior:**
    *   Texto en blanco: `HUD DRAG v1.0.4942 F__00200 FPS 49.90` (en la primera imagen) que cambia a `F__00100 FPS 44.90` (segunda) y `F__00500 FPS 41.90` (tercera).
    *   Texto en amarillo: `D:0 SRC:02 DST:--` (primera), `D:0 SRC:00 DST:--` (segunda), `D:2 SRC:00 DST:--` (tercera).
    *   Una fila de iconos gráficos en la parte superior de la barra: `■+♦+` (primera), `■♦♦+` (segunda), `■♦♦+` (tercera). El icono `■` (cuadrado rojo) está resaltado con un borde rojo en la segunda y tercera imagen.

**Elementos que cambian:**
*   **Iconos gráficos en la barra inferior:**
    *   En la primera imagen, el icono `■` (cuadrado rojo) está resaltado con un borde rojo.
    *   En la segunda y tercera imagen, el icono `■` (cuadrado rojo) está resaltado con un borde rojo, pero el icono `♦` (diamante rojo) a su izquierda ha cambiado de color a amarillo.
    *   El icono `♦` (diamante rojo) a la derecha del cuadrado rojo también cambia de color a amarillo en la segunda y tercera imagen.
*   **Texto en la barra inferior:**
    *   El valor de `D:` cambia de `0` a `4` y luego a `2`.
    *   El valor de `SRC:` cambia de `02` a `00` y luego a `00`.
    *   El valor de `FPS` disminuye de `49.90` a `44.90` y luego a `41.90`.
    *   El valor de `F__` cambia de `00200` a `00100` y luego a `00500`.

**Observaciones sobre los contornos y el fondo:**
*   Los contornos de los iconos gráficos (cuadrado rojo, diamantes rojos) son limpios y no presentan bandas, halos ni píxeles cortados.
*   El fondo inmediatamente alrededor de los iconos gráficos es negro, y no hay artefactos visibles.
*   No hay elementos superpuestos o incompletos.

**Conclusión:** Las imágenes muestran una interfaz de usuario que cambia dinámicamente. Los elementos gráficos fijos (rectángulos azules, barra oscura) permanecen en la misma posición. Los iconos gráficos en la barra inferior cambian de color y posición, y el texto en la barra inferior cambia constantemente, indicando una interacción o un proceso en curso. No hay artefactos visibles en los contornos ni en el fondo.
