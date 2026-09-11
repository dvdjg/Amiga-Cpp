# Prompt de porte 1:1 desde `demoscene-repo-orig`

Prompt y método reutilizable para traer efectos del repo demoscene al engine **tal cual**, sin reinventar. Se usa demo a demo (p. ej. `effects/wireframe/wireframe.c`, luego `effects/fire-rgb/fire-rgb.c`).

## Regla de oro

La **fuente de verdad** es `demoscene-repo-orig/` **y su `.exe` ya compilado**. El trabajo es **PORTAR, no reimplementar**. Está prohibido escribir líneas, rellenos, matrices, C2P o cualquier algoritmo que ya exista en `lib/`: si existe, se copia su lógica y su secuencia de registros **verbatim**.

## Prompt (pegar tal cual, cambiando `<efecto>`)

> La fuente de verdad es `demoscene-repo-orig/` y su `.exe` ya compilado. Vas a **PORTAR**, no reimplementar. Prohibido escribir líneas/rellenos/matrices/C2P que ya existan en `lib/`; si existe, se copia su lógica y sus registros tal cual. Para `effects/<efecto>/<efecto>.c`, en este orden:
> 1. **Inventario de la rebanada**: lista TODOS los ficheros necesarios (el `.c`, sus `#include`, las libs de `lib/`, los `data/*.c`, y el runtime). Entrégala antes de tocar nada; ese es el alcance del porte.
> 2. **Separa hot de fronteras**: hot = math fixed-point, secuencias de registros Blitter/Copper, recorrido del object model → se porta **verbatim** (mismas macros, mismos minterms, mismo orden de escritura, mismo fixed-point). Fronteras = alloc, playfield/coplist, vblank, arranque/parada → se re-expresan con `MinimalBackend`/arenas/copper/`FramePlan` **sin cambiar el algoritmo**.
> 3. **Tabla de mapeo de fronteras** (`NewBitmap→arena`, `SetupPlayfield/LoadColors→StaticEhbScene`, `TaskWaitVBlank→wait_vblank`, `custom->X→MinimalBackend`, `EnableDMA→backend`…) antes de implementar.
> 4. **Fidelidad byte a byte del hot**: copia literal `bltcon0/1`, `bltamod/bmod`, `bltapt`, `bltsize`, `MULVERTEX1/2`, `div16`, `normfx`, y el formato empaquetado `obj2c` (`FACE/EDGE/NODE3D/VERTEX`). Revisa el asm (`-S`) y ajústalo hasta igualar al original.
> 5. **Oráculo + iteración hasta 1:1**: ejecuta el `.exe` original y tu porte en WinUAE, captura los mismos frames, **compara pixel a pixel** y usa visión local (`node tools/analyze/ollama-desc.mjs`) para describir ambos. Nada se da por bueno sin esa comparación.
> 6. **Una demo a la vez** (`demos/amiga/NNN_<efecto>`), con READY, assertion y nota de "verbatim vs adaptado". Siguiente solo cuando la anterior esté 1:1.
>
> Reglas duras: "más lenta pero fiel" no vale; "nueva y más limpia" tampoco. Ante duda de un registro, gana el original, no la documentación.

## Flujo operativo

```
1. Inventario rebanada  ->  2. hot vs fronteras  ->  3. tabla de mapeo
        |                                                        |
        v                                                        v
4. porte verbatim del hot  <---------------------------  fronteras al backend
        |
        v
5. build -> run -> diff de frames vs el .exe original (readPng + ollama-desc)
        |
        v
6. 1:1?  --no--> volver a 4 mirando QUE registro/algoritmo difiere
        |
        si
        v
7. cerrar demo + documentar (verbatim vs adaptado); siguiente efecto
```

## Herramientas de comparación

- Captura del original y del porte: runner de demos (`out/run/<demo>/<cfg>/screenshot.png` y `sequence/frame_NNN.png`).
- Diff de píxeles: `dist/tools/lib/image.js` (`readPng`, `pixel`).
- Descripción con visión: `node tools/analyze/ollama-desc.mjs <dir> <idx> "<prompt>"` (modelo `qwen3-vl:8b-instruct-q8_0`).
- Símbolos del original para depurar: `effects/<efecto>/<efecto>.exe.map` / `.dbg`.

## Errores a evitar (lecciones)

- **No** derivar un algoritmo de la prosa de la documentación si existe código de referencia (pasó con el area-fill del Blitter).
- **No** sustituir una rutina caliente por una "equivalente nueva" (pasó con la línea Bresenham de CPU frente a la línea por Blitter del original).
- **Sí** copiar la línea y el relleno del propio efecto (`DrawObject`, `BlitterFillArea`) y ajustar solo las fronteras.
