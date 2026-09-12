# HOST-021 — Datos del plasma (tablas + paleta)

Fija los datos del porte de `effects/plasma`: las tablas `tab1/2/3` y la **paleta** de 256
colores, idénticas al original.

## Qué cubre

- **`tab1/2/3`** (`demos/amiga/082_plasma/src/data/plasma_tables.hpp`): `fx4i(3·47/31/37) ·
  SIN/COS(i·32) >> 16` **verbatim** (con la sintab exacta y el `>>16` aritmético). Checksums
  `-425422980 / -1961918596 / 753422204` y valor concreto `tab[64]`.
- **Paleta** (`plasma_colors.hpp`): 256 × RGB12 extraídos del original
  (`plasma-colors.c`). Checksum `402750400`.

## Nota de fidelidad

El original asigna las tablas a `char` (s8): los valores ±141 **envuelven** a s8; el test lo
fija (rango −128..127). `fx4i(i)=i<<4`; `SIN`/`COS` = `math2d::sin_q12`/`cos_q12` (sintab 4.12
exacta, HOST-020).

## Ejecución

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/021_plasma_tables
```

Contexto: `docs/demos/effects/PLASMA_PORT_PLAN.md`.
