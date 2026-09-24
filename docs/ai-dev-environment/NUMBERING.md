# Numeración por ámbito (anti-solape de demos y tests)

El número de una demo o un test host es **único dentro de su ámbito** (no global). Un **ámbito** es
el directorio que contiene directamente los elementos numerados:

- **Tests host**: `tests/host/<categoría>/` (p. ej. `tests/host/graphics`).
- **Demos**: `demos/techniques/<familia>/<categoría>/` (p. ej. `demos/techniques/amiga/copper`) y
  `demos/features/<feature>/<plataforma>/` (p. ej. `demos/features/ui/amiga`).

Así, `demos/features/ui/amiga/007_foo` y `demos/features/ui/megadrive/007_foo` **no colisionan**
(el path difiere), y dos ramas que trabajan en ámbitos distintos tampoco. Este documento es la
**fuente única de verdad** de la numeración. Contexto de la organización de demos:
[`PLAN_ORGANIZACION_DEMOS.md`](../guides/roadmap/PLAN_ORGANIZACION_DEMOS.md).

## Regla

1. **Único dentro del ámbito.** Un número `NNN` no se reutiliza dentro del mismo ámbito.
2. **Ámbitos separados no colisionan.** Dos ramas en ámbitos distintos numeran libremente.
3. **Mismo ámbito entre ramas → sub-bloques.** Si dos ramas activas van a crear numerados en el
   **mismo** ámbito, se reparte en **sub-bloques** (§Sub-bloques) antes de crear nada. Compartir un
   sub-bloque es lo que provoca las colisiones.
4. **Los checks cierran el merge.** `tools/check/test-numbering.mjs` (tests, con catálogo por
   categoría) y `tools/check/demo-numbering.mjs` (demos, por ámbito) abortan si hay duplicados o
   leafs repetidos; el merge no cuela un solape aunque se olvide el reparto.
5. **Colisión**: se renumera **una** de las dos (la de menor impacto: menos referencias y/o en la
   rama) y se actualizan directorio, título, catálogo (tests) y todas las referencias.

## Sub-bloques (mismo ámbito, varias ramas)

| Ámbito | Rango | Rama / workstream | Estado |
|---|---|---|---|
| (todos) | 000-999 | `master` (por defecto) | en uso |

> Hoy **todo lo numera `master`**; los ámbitos son disjuntos por construcción (paths distintos). Si
> una rama nueva va a crear demos/tests en un ámbito que ya toca `master` (o otra rama), añade una
> fila aquí reservando un **rango contiguo** (p. ej. `demos/techniques/amiga/blitter: 100-199 →
> rama/foo`) **antes** de crear nada, y numera dentro de él.

## Herramientas

- `node tools/check/demo-numbering.mjs` — demos: sin duplicados por ámbito y sin leafs repetidos.
- `node tools/check/test-numbering.mjs` — tests host: sin duplicados y catálogo por categoría sincronizado.
- `node tools/check/next-number.mjs <ámbito>` — siguiente número libre en un ámbito, p. ej.
  `node tools/check/next-number.mjs demos/techniques/amiga/copper`.

Los dos primeros corren en la pasada completa de `tools/run-host-tests.sh`; el tercero es de apoyo.

## Histórico (modelo anterior, global por rama)

Antes de los ámbitos, la numeración era **global** y se repartía en **bloques por rama**
(`A` 000-099 histórico, `B` 100-149 `master`, `C` 150-198 mixto, `D` 199-299 `master`, `E` 300-399
`feature/optimize`, `F` 400-499 libre). Fue la fuente de **4 colisiones reales** al fusionar
(`206`, `208`, `252`, `261`), porque dos ramas podían elegir el mismo número. Con ámbitos disjuntos
esa situación no puede repetirse. Los números ya usados **no se reutilizan** y el antiguo reparto
por rama queda **obsoleto**: a partir de ahora, la coordinación es por ámbito (§Sub-bloques).
