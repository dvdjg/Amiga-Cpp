# HOST-178: generador de doble hélice de `dna3d` (port a `Turns`)

Porta `GenCircularDoubleHelix` de `effects/dna3d/dna3d.c` al vocabulario del engine. El
original mide el ángulo como **índice** (`SIN(a) = sintab[a & 0xfff]`, `include/fx.h`); el
port usa `sin(turns(idx))` (`Turns`, tabla 4.12 exacta). Es el primer paso del plan de port
de `dna3d` (`docs/demos/effects/DNA3D_PORT_PLAN.md`).

## Qué comprueba

1. **Equivalencia con el original**: una transcripción 1:1 de `GenCircularDoubleHelix` (con
   `kSinTab` directo y los `swap16`/shifts) contra el port con `Turns`/`sin`/`cos`, para
   varias fases `phi_offset` (de −2048 a 2048). Punto a punto, exacto.
2. **Geometría**: la hélice cabe en su caja (`|z| ≤ 256`), i.e. los shifts del original son
   los del port.

## Por qué existe

`dna3d` regenera la hélice cada frame con trigonometría; portarla mal (un shift, un signo,
el `swap16`) deforma la figura sin fallar el build. El test fija el algoritmo antes de
escribir la demo, y usa la tabla exacta como referencia compartida.

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/178_dna_helix
```
