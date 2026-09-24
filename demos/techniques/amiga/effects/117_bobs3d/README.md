# Demo 117 — bobs3d

Porte del efecto `demoscene-repo-orig/effects/bobs3d/bobs3d.c`. Ver el análisis completo en
[`docs/demos/effects/BOBS3D_PORT_PLAN.md`](../../../docs/demos/effects/BOBS3D_PORT_PLAN.md).

El objeto `pilka` (malla `obj2c`) rota y **cada vértice proyectado se dibuja como un BOB OR
intercalado** (chispa 48x32x3, un blit por objeto) sobre un playfield de 3 planos. El fondo
es un segundo playfield (*carrion-metro*, 2 planos) con la paleta reescrita por línea por el
Copper (doble playfield 3+2).

## Ejecutar

```bash
bash ./tools/build/build-demo.sh demos/techniques/amiga/effects/117_bobs3d --debug
bash ./tools/run/run-demo.sh demos/techniques/amiga/effects/117_bobs3d
node tools/debug/measure-fps.mjs 117_bobs3d A500_debug
```

## Dial fidelidad ↔ fps: `K_117_MAXBLOBS`

El efecto original dibuja **60 BOBs** (uno por vértice). El nº dibujado es configurable:

| `K_117_MAXBLOBS` | BOBs | campos/frame | fps |
|---|---|---|---|
| 60 | 60 (fiel) | 3,0 | ~16,6 |
| 58 | 58 | 2,3 | ~21,8 |
| **56 (defecto)** | 56 | **2,0** | **~25** |

Con **56** el `update` cabe en 2 campos (25 fps) manteniendo el sincronismo estricto de 2
buffers. `>=64` dibuja los 60 (como el original). Para los 60 BOBs a 2 campos habría que
recortar ~2,5k de `load_rotate` (medido 2.676 c), sin vía limpia.

## Diagnóstico por capas

Interruptores de compilación para aislar componentes (validación incremental con visión):

```bash
EXTRA_DEFINES="-DK_117_BG=0"                 bash ./tools/build/build-demo.sh demos/techniques/amiga/effects/117_bobs3d --debug  # BOBs sobre negro
EXTRA_DEFINES="-DK_117_BOBS=0"               bash ./tools/build/build-demo.sh demos/techniques/amiga/effects/117_bobs3d --debug  # solo fondo
EXTRA_DEFINES="-DK_117_BATCH=0"              bash ./tools/build/build-demo.sh demos/techniques/amiga/effects/117_bobs3d --debug  # camino generico (bob.hpp/FramePlan)
EXTRA_DEFINES="-DK_117_MAXBLOBS=60"          bash ./tools/build/build-demo.sh demos/techniques/amiga/effects/117_bobs3d --debug  # 60 BOBs (fiel, ~20 fps)
EXTRA_DEFINES="-DK_117_IRQ=1"                bash ./tools/build/build-demo.sh demos/techniques/amiga/effects/117_bobs3d --debug  # cadencia IRQ (requiere triple buffer)
EXTRA_DEFINES="-DK_117_PROF=0"               bash ./tools/build/build-demo.sh demos/techniques/amiga/effects/117_bobs3d --debug  # sin instrumentacion
```

Por defecto se usa el **lote de BOBs** del backend (`eng::amiga::OrBlobBatch`): constantes
del blit fijadas una vez, atlas denso y 3 palabras fieles al original, sin `jsr` por BOB. El
camino genérico (`graphics::bob` + `FramePlan`) se conserva para comparar (`K_117_BATCH=0`).

Sincronía: **2 buffers + `run_frames_polling`** (update alineado a VBlank) + clear solapado
con el transform. Sin tearing. La fase del clear (`kProfClear`), el transform, el lote y el
`install` se miden con `ENG_PROF` (secciones `clear/transform/draw/blits/install/update/forward`).
Referencia de coste, cuellos de botella y decisiones: `BOBS3D_PORT_PLAN.md` §6–§9.
