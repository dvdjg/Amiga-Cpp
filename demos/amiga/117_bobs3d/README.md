# Demo 117 — bobs3d

Porte del efecto `demoscene-repo-orig/effects/bobs3d/bobs3d.c`. Ver el análisis completo en
[`docs/demos/effects/BOBS3D_PORT_PLAN.md`](../../../docs/demos/effects/BOBS3D_PORT_PLAN.md).

El objeto `pilka` (malla `obj2c`) rota y **cada vértice proyectado se dibuja como un BOB OR
intercalado** (chispa 48x32x3, un blit por objeto) sobre un playfield de 3 planos. El fondo
es un segundo playfield (*carrion-metro*, 2 planos) con la paleta reescrita por línea por el
Copper (doble playfield 3+2).

## Ejecutar

```bash
bash ./tools/build/build-demo.sh demos/amiga/117_bobs3d --debug
bash ./tools/run/run-demo.sh demos/amiga/117_bobs3d
node tools/debug/measure-fps.mjs 117_bobs3d A500_debug
```

## Diagnóstico por capas

Interruptores de compilación para aislar componentes (validación incremental con visión):

```bash
EXTRA_DEFINES="-DK_117_BG=0"     bash ./tools/build/build-demo.sh demos/amiga/117_bobs3d --debug   # BOBs sobre negro
EXTRA_DEFINES="-DK_117_BOBS=0"   bash ./tools/build/build-demo.sh demos/amiga/117_bobs3d --debug   # solo fondo
EXTRA_DEFINES="-DK_117_BATCH=0"  bash ./tools/build/build-demo.sh demos/amiga/117_bobs3d --debug   # camino generico (bob.hpp/FramePlan)
```

Por defecto se usa el **lote de BOBs** del backend (`MinimalBackend::blitter_or_bobs`):
constantes del blit fijadas una vez, atlas denso y 3 palabras fieles al original. El
camino genérico (`graphics::bob` + `FramePlan`) se conserva para comparar (`K_117_BATCH=0`).

Las secciones `ENG_PROF_*` (`clear`, `transform`, `draw`, `blits`, `install`) se leen con
`tools/debug/profile.mjs` o con el lector de ciclos. Referencia de coste y cuellos de botella
en el plan de porte (§6–§7).
