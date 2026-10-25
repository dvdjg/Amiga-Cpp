# Informe de ejecución — 500_caverna

Crudo de las pasadas de visión/medida. Convención: docs/guides/methodology/DEMO_VISUAL_DEBUG.md §6.5.
El resumen canónico commiteado vive en el VALIDATION.md de la demo.

## Pasada 2026-10-25T15:35:50.470Z

### Medición (`measure-fps`, ventana de 20 s)

```text
500_caverna/A500_release (estático)   | emulado=49,92 fps | host=49,86 | 142102 ciclos/frame | 1,002 campos/frame | stable=true
500_caverna/A500_release (1 px/frame) | emulado=49,82 fps | host=49,78 | 142384 ciclos/frame | 1,004 campos/frame | stable=true
500_caverna/A500_release (2 px/frame) | emulado=40,80 fps | host=40,74 | 173868 ciclos/frame | 1,225 campos/frame | inestable
```

La pasada de 1 px/frame usa la instrumentación de auto-scroll (`EXTRA_DEFINES=-DCAVERNA_AUTOSCROLL=1`, no presente en el binario final) y la misma ruta de scroll que la entrada real. La de 2 px/frame es el A/B que motiva el paso de 1 px/frame.

### Fix de scroll (antes/después)

Síntoma: al desplazar la cámara aparecía un bloque rectangular de terreno PF2 en la zona superior derecha; persistía al detener el movimiento y no aparecía en cámara estática.

Causa: el walk horizontal dibuja hasta `mapy = 17`; con el anillo en `display_height = 208` (13 tiles) o 240 (15 tiles) esa fila de staging caía en la ventana visible. Las filas 14..19 del mapa PF2 contenían roca/tierra decorativa que no es visible con `y_mode=Off`.

Fix: `caverna_art.cpp` deja vacías las filas 14..19 y el anillo usa `viewport_h + 2·tile` (240 px). Verificación: captura paso a paso (`--sequence-step-frames`) después de `--joy 1:right --joy-hold-ms 2000`; el bloque desaparece y el fondo queda continuo.

### Secuencia de visión

- Secuencia: `out/run/500_caverna/A500_release/sequence`
- Frames: frame_000.png … frame_005.png (6); modelo qwen3-vl:8b-instruct-q8_0; prompt canónico (`tools/analyze/vision-prompt.txt`).
- Evaluación completa: `src/VISION.md`.

Respuesta cruda (extracto de `src/VISION.md`):

```text
Las tres imágenes muestran una escena de un videojuego en estilo pixel art, con un fondo de paredes oscuras y una base de tierra marrón. El jugador controla un personaje naranja en el centro de la pantalla. Hay varios objetos en el entorno: dos triángulos rojos en la parte superior izquierda, dos triángulos naranjas más abajo, un objeto verde en la parte superior derecha, y un objeto rosa en la parte izquierda.

Cambios entre imágenes:
- Imagen 1 vs. Imagen 2: El objeto rosa en la parte izquierda se ha desplazado ligeramente hacia la izquierda. El resto de los elementos permanece en la misma posición.
- Imagen 2 vs. Imagen 3: El objeto rosa ha desaparecido completamente. El resto de los elementos permanece en la misma posición.

Contorno de los objetos: limpios y bien definidos. No hay bandas, halos ni píxeles cortados.
Fondo alrededor de los objetos: permanece igual, sin artefactos visibles.
Elementos extraños o defectuosos: no se observan superposiciones, recortes, artefactos ni incoherencias.
```

Band-diff `check-elements` (frame_000 vs frame_005): cambios medios solo en las bandas de enemigos (y=96..127 y 160..207); las bandas de fondo/terreno quedan estáticas (diff 0,00).

### Prueba de salto

`vision-jump.mjs out/run/500_caverna/A500_release/sequence --require` → `SALTO: NO`. La inyección de `--joy 1:up` no produce una subida/bajada que el VLM confirme; queda **NO VERIFICADO**.
