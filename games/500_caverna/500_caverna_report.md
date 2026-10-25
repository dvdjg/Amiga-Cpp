# Informe de ejecución — 500_caverna

Crudo de las pasadas de visión/medida. Convención: docs/guides/methodology/DEMO_VISUAL_DEBUG.md §6.5.
El resumen canónico commiteado vive en el VALIDATION.md de la demo.

## Pasada 2026-10-25T15:51:09.981Z

### Medición (`measure-fps`, ventana de 20 s)

```text
500_caverna/A500_release (estático)              | emulado=49,82 fps | host=49,75 | 142384 ciclos/frame | 1,004 campos/frame | stable=true
500_caverna/A500_release (joystick der., 2 px/f) | emulado=49,87 fps | host=49,83 | 142242 ciclos/frame | 1,003 campos/frame | stable=true
```

La fila en movimiento usa entrada real sostenida (`measure-fps --joy 1:right`, el mismo camino de eventos que `run-demo --joy`): el jugador avanza 2 px/frame y la cámara le sigue.

### Multiplexado y parcheo (A/B)

Con nueve actores (jugador + ocho enemigos), el `SpriteDirector` reutiliza un canal por multiplexado vertical. La tabla de parcheo tenía **un slot por canal** y rechazaba el canal repetido, así que el compositor dual reemitía la copperlist completa: **31,65 fps**. Se generalizó `SpritePatchTable` a **un slot por placement** (conservando `channel[]` para el primer placement de cada canal) y `emit_placements_patchable` emite también los rearmes del canal reutilizado. Resultado con entrada real y 2 px/frame: **49,87 fps**.

### Fix de scroll (antes/después)

Síntoma: al desplazar la cámara aparecía un bloque rectangular de terreno PF2 en la zona superior derecha; persistía al detener el movimiento y no aparecía en cámara estática.

Causa: el walk horizontal lee hasta `mapy = 17` (fila de staging). Las filas 14..19 del mapa PF2 contenían roca/tierra decorativa que no es visible con `y_mode=Off`; al scrollear, esa fila caía en la ventana visible.

Fix: `caverna_art.cpp` deja vacías las filas 14..19 y el anillo usa `viewport_h + 2·tile` (240 px). Verificación: captura paso a paso (`--sequence-step-frames`) después de `--joy 1:right --joy-hold-ms 2000`; el bloque desaparece y el fondo queda continuo.

### Secuencia de visión

- Secuencia: `out/run/500_caverna/A500_release/sequence`
- Frames: frame_000.png … frame_005.png (6); modelo qwen3-vl:8b-instruct-q8_0; prompt canónico (`tools/analyze/vision-prompt.txt`).
- Evaluación completa: `src/VISION.md`.

Respuesta cruda (extracto de `src/VISION.md`):

```text
Las tres imágenes muestran una escena de un videojuego de estilo pixel art, con un fondo de paredes de ladrillo oscuras y un suelo de tierra marrón. El jugador, un personaje naranja, está en el centro de la pantalla. Hay varios enemigos y dos triángulos rojos en la parte superior izquierda; una estructura vertical de bloques azules y blancos está en la parte superior derecha.

Cambios entre imágenes: el triángulo naranja desaparece y aparecen dos triángulos rojos; el fondo, el terreno y el personaje permanecen sin cambios.

No hay artefactos visibles como bandas, halos o píxeles cortados. Los contornos de los objetos son limpios y no hay restos de su posición anterior. El fondo inmediatamente alrededor de los objetos permanece igual.
```

Band-diff `check-elements` (frame_000 vs frame_005): cambios en el HUD (y=0..15) y en las bandas de enemigos (y=96..127 y 160..191); las bandas de fondo/terreno quedan estáticas (diff 0,00).

### Prueba de salto

`vision-jump.mjs out/run/500_caverna/A500_release/sequence --require` → `SALTO: NO`. La inyección de `--joy 1:up` no produce una subida/bajada que el VLM confirme; queda **NO VERIFICADO**.
