# EVALUACIÓN DE VISIÓN — 080_camera_pan_window

<!-- generado por tools/analyze/vision-final.mjs; no editar a mano -->

- Fecha: 2026-10-21T00:54:08.202Z
- Modelo: `qwen3-vl:8b-instruct-q8_0`
- Frames: frame_000.png, frame_037.png, frame_074.png, frame_111.png, frame_148.png, frame_185.png, frame_222.png, frame_259.png (8)
- Secuencia: `out/run/080_camera_pan_window/A500_debug/sequence`

## Prompt (canónico, tools/analyze/vision-prompt.txt)

```text
Las imágenes adjuntas van EN ORDEN TEMPORAL (si hay más de una). Describe con precisión qué ves: todos los elementos gráficos que aparecen, dónde están situados y qué hacen. Indica cualquier elemento que cambie entre imágenes y cualquier cosa que parezca extraña, defectuosa o incoherente (posiciones raras, parpadeos, recortes, artefactos, elementos superpuestos o incompletos). Presta especial atención a los objetos que se mueven: examina su CONTORNO en cada imagen (¿está limpio el perfil del objeto, sin banda o halo rectangular alrededor, sin restos de su posición anterior, sin píxeles cortados o colgando en los bordes?) y el FONDO inmediatamente alrededor (¿queda igual que cuando el objeto no estaba encima, o el objeto lo tapa con un rectángulo de color uniforme?). No adivines el propósito del programa: describe solo lo observable. Responde de forma concisa y ordenada.
```

## Respuesta (cruda)

**Imagen 1:**
- **Escena:** Vista frontal de un pasillo o sala con paredes azules, techo verde y suelo marrón. En el centro, hay cuatro columnas verticales: dos azules cian a los lados, una amarilla en el centro izquierdo y una naranja en el centro derecho.
- **Texto en la parte inferior:** `080 CAMERA PAN WINDOW`, `FULL 12.5 + FAKE 25`, `FRAME 04FUELLO FAKEFAKE 337`, `MODE YAW Y 09PP00KK00`.
- **Estado:** La cámara está centrada en las columnas.

**Imagen 2:**
- **Escena:** La cámara ha girado hacia la derecha. La vista ahora muestra principalmente una pared azul, con el suelo marrón en la parte inferior derecha. Las columnas están parcialmente visibles en el borde derecho.
- **Texto en la parte inferior:** `080 CAMERA PAN WINDOW`, `FULL 12.5 + FAKE 25`, `FRAME 8848FULLL2162FAKE6997`, `MODE PANCY 446 P 09KK00`.
- **Estado:** La cámara ha realizado un movimiento de panning (girar horizontalmente).

**Imagen 3:**
- **Escena:** La cámara ha girado hacia la izquierda. La vista muestra ahora una pared verde en el techo y las paredes laterales, con las columnas visibles en el centro. El suelo es marrón.
- **Texto en la parte inferior:** `080 CAMERA PAN WINDOW`, `FULL 12.5 + FAKE 25`, `FRAME 2696 FULL 534 FAKE 1879`, `MODE BISCAJOU8 P 019 K K 0`.
- **Estado:** La cámara ha girado hacia la izquierda desde la posición de la imagen 2.

**Imagen 4:**
- **Escena:** La cámara ha girado hacia la derecha nuevamente. Las columnas están ahora en el lado derecho de la pantalla, con el suelo marrón en la parte inferior y una pared azul en el fondo.
- **Texto en la parte inferior:** `080 CAMERA PAN WINDOW`, `FULL 12.5 + FAKE 25`, `FRAME 3560 FULL 650 FAKE 2931`, `MODE EANSBJOUPY0-K P 3 K 0`.
- **Estado:** La cámara ha girado hacia la derecha desde la posición de la imagen 3.

**Cambios entre imágenes:**
- Las columnas se mueven horizontalmente en la pantalla a medida que la cámara gira, pero sus contornos son limpios y no presentan bandas, halos ni píxeles cortados.
- El fondo (paredes, techo, suelo) cambia de forma coherente con el movimiento de la cámara, sin artefactos visibles.
- El texto en la parte inferior cambia en cada imagen, reflejando el estado de la cámara y el marco de video, pero no hay elementos superpuestos ni incompletos.
- No hay parpadeos ni recortes visibles en ninguna de las imágenes.
