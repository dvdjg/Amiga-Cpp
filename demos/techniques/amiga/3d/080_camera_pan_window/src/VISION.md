# EVALUACIÓN DE VISIÓN — 080_camera_pan_window

<!-- generado por tools/analyze/vision-final.mjs; no editar a mano -->

- Fecha: 2026-10-18T10:36:30.493Z
- Modelo: `qwen3-vl:8b-instruct-q8_0`
- Frames: frame_000_f0009.png, frame_002_f0011.png, frame_004_f0013.png, frame_005_f0014.png, frame_007_f0016.png (5)
- Secuencia: `out/run/080_camera_pan_window/A500_debug/sequence`

## Prompt (canónico, tools/analyze/vision-prompt.txt)

```text
Las imágenes adjuntas van EN ORDEN TEMPORAL (si hay más de una). Describe con precisión qué ves: todos los elementos gráficos que aparecen, dónde están situados y qué hacen. Indica cualquier elemento que cambie entre imágenes y cualquier cosa que parezca extraña, defectuosa o incoherente (posiciones raras, parpadeos, recortes, artefactos, elementos superpuestos o incompletos). No adivines el propósito del programa: describe solo lo observable. Responde de forma concisa y ordenada.
```

## Respuesta (cruda)

Las tres imágenes muestran una escena gráfica estática con una interfaz de texto en la parte inferior, y presentan cambios sutiles en los valores numéricos de la interfaz, mientras que la escena visual permanece inalterada.

**Escena gráfica (constante en todas las imágenes):**
*   **Fondo:** Un fondo de color azul oscuro.
*   **Ventana de visualización:** Un rectángulo con bordes ligeramente desenfocados, que contiene la escena principal.
*   **Escena interior:**
    *   A la izquierda, un gran triángulo verde que parece ser una representación de un haz de luz o una pared inclinada.
    *   A la derecha, una superficie marrón que simula el suelo.
    *   En el centro, cuatro barras verticales de colores: dos azules cian, una amarilla y una naranja. Están dispuestas en dos pares, con un espacio entre ellos.

**Interfaz de texto (cambia entre imágenes):**
*   **Línea 1:** `080 CAMERA PAN WINDOW`
*   **Línea 2:** `FULL 25HZ + FAKE 50HZ`
*   **Línea 3:** `FRAME 0 FULL 1 FAKE 0` → cambia a `FRAME 16 FULL 10 FAKE 7` en la tercera imagen.
*   **Línea 4:** `FULL OFF 0 FAC 7 VER 28 PAN -128` → cambia a `FAKE OFF 1 FAC 9 VER 39 PAN -67` en la tercera imagen.

**Cambios observables:**
*   La escena gráfica no cambia en absoluto entre las imágenes.
*   Los valores numéricos en la interfaz de texto cambian solo en la tercera imagen, específicamente en las líneas 3 y 4.
*   El texto "FULL OFF 0 FAC 7 VER 28 PAN -128" en la segunda imagen se convierte en "FAKE OFF 1 FAC 9 VER 39 PAN -67" en la tercera, lo que sugiere un cambio de estado o configuración del sistema.

**Elementos extraños o incoherentes:**
*   No hay artefactos visuales, parpadeos ni elementos superpuestos.
*   La escena es muy simple y abstracta, pero no presenta elementos incoherentes o defectuosos desde el punto de vista de la visualización. La interfaz de texto parece ser una salida de un programa de prueba o de configuración.
