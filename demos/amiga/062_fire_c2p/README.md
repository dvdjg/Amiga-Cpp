# Demo 062: fuego 4bpl + c2p (port de effects/fire-rgb)

Importa el efecto `05-fire-rgb` de `demoscene-repo-orig` adaptado a la nueva
estructura del engine. **Fiel al original**: el fuego se calcula a 80×64
(`WIDTH=80`, `HEIGHT=64`) y se muestra a 320×256 (escalado 4×4 pixelado), como el
efecto original.

## Qué valida

- **`c2p_1x1_4`** con datos reales animados (fuego chunky → 4 planos).
- El patrón de importación: effect reconstruido con APIs del engine, sin degradar
  la apariencia (regla 6 de `DEMOSCENE_EFFECT_REPLICATION_POLICY.md`).

## Fidelidad visual (diferencias con el original)

| Original (`fire-rgb`) | Esta demo | Equivalencia visual |
|---|---|---|
| HAM mode (6-7 planos) | 4 bitplanes + paleta de fuego | colores de fuego rojo→amarillo |
| c2p por **Blitter** (fases swap 8x4/4x4) | c2p por **CPU** (`c2p_1x1_4` de Kalms) | mismo resultado planar |
| 80×64 + fetch ancho + line-quadrupling (`BPLMOD`) | 80×64 + `scale4x` (replica byte 4x horiz + línea 4x vert) | **misma imagen pixelada 4×4** |
| `dualtab` (LUT HAM) | paleta directa 0..15 | — |

El tamaño de trabajo (80×64) y el escalado 4×4 a 320×256 se conservan del
original, de modo que la réplica se ve igual: llamas pixeladas que suben desde la
parte inferior y ocupan todo el ancho.

## Lección de rendimiento

El cuello de botella NO es el c2p, sino el algoritmo del fuego O(W·H). A 80×64
(5120 px) es rápido (~35 ms/iteración); el c2p de 80×64 es despreciable. El
original usa 80×64 por esta razón (no 320×256 de cálculo).

## Build & run

```bash
tools/build/build-demo.sh demos/amiga/062_fire_c2p --clean
tools/run/run-demo.sh       demos/amiga/062_fire_c2p --warp
```

## Criterio de aceptación

- Compila y llega a `Ready`.
- Captura: llamas pixeladas (bloques 4×4) rojas/naranjas subiendo desde abajo,
  ocupando todo el ancho.
