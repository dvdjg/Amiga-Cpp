# EVALUACIÓN DE VISIÓN — 000_template

<!-- generado por tools/analyze/vision-final.mjs; no editar a mano -->

- Fecha: 2026-10-17T19:02:34.461Z
- Modelo: `qwen3-vl:8b-instruct-q8_0`
- Frames: frame_000.png, frame_002.png, frame_003.png, frame_005.png (4)
- Secuencia: `out\tmp\000_template_seq_title`

## Prompt (canónico, tools/analyze/vision-prompt.txt)

```text
Las imágenes adjuntas van EN ORDEN TEMPORAL (si hay más de una). Describe con precisión qué ves: todos los elementos gráficos que aparecen, dónde están situados y qué hacen. Indica cualquier elemento que cambie entre imágenes y cualquier cosa que parezca extraña, defectuosa o incoherente (posiciones raras, parpadeos, recortes, artefactos, elementos superpuestos o incompletos). No adivines el propósito del programa: describe solo lo observable. Responde de forma concisa y ordenada.
```

## Respuesta (cruda)

**Imagen 1:**
- **Fondo:** Un rectángulo azul sólido que ocupa casi toda la pantalla, con un borde negro alrededor.
- **Elemento central:** Un rectángulo verde sólido, horizontal, centrado en el fondo azul.
- **Texto en la parte inferior:** En la esquina inferior izquierda, se muestra el texto "TEMPLATE v1.0.4414 F 50" en letras blancas, de estilo pixelado. La "0" final del número "50" está parcialmente recortada por el borde inferior de la pantalla.

**Imagen 2:**
- **Fondo:** Igual que en la imagen 1.
- **Elemento central:** Igual que en la imagen 1.
- **Texto en la parte inferior:** El texto es idéntico al de la imagen 1, pero ahora la "0" final del número "50" está completamente visible, sin recorte.

**Cambio entre imágenes:**
- El único cambio observable es en el texto de la esquina inferior izquierda. En la imagen 1, el último dígito del número "50" está recortado; en la imagen 2, está completo. El resto de la pantalla permanece idéntico.
