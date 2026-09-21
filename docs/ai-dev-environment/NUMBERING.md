# Numeración por rama (anti-solape de demos y tests)

Las demos (`demos/<plataforma>/NNN_<tema>/`) y los tests host (`tests/host/NNN_<nombre>/`) usan
un número `NNN` **único y no reutilizable**. Con varias ramas activas a la vez, dos ramas pueden
elegir el mismo número y el merge produce duplicados (caso real: `feature/optimize` numeró
`152-171` para board/cards y `master` `152-178` para sim/engine). Para que no vuelva a pasar, la
numeración se reparte en **bloques reservados por rama/workstream**, con este documento como
**fuente única de verdad**.

## Regla

1. **Bloques de 100**. Cada rama/workstream que cree demos o tests numerados **reserva un bloque
   contiguo** antes de crear nada. Los números de un bloque se usan **solo** en su rama.
2. **Registro único**: la tabla de §Bloques es la fuente de verdad. Reservar = editar esta tabla
   en `master` (un commit pequeño) y luego trabajar dentro del bloque en la rama.
3. **Integra `master` antes de numerar**: `git merge master` para ver los números ya usados y
   elegir el siguiente libre **dentro de tu bloque**.
4. **Los checks cierran el merge**: `tools/check/test-numbering.mjs` (tests, con catálogo) y
   `tools/check/demo-numbering.mjs` (demos) abortan si hay números duplicados; el merge no puede
   colar un solape aunque se olvide la reserva.
5. **Colisión**: si dos ramas usan el mismo número, se renumera **una** (la del bloque más
   nuevo) y se actualizan directorio, título, catálogo (tests) y **todas** las referencias en
   docs, incluidos los rangos `HOST-x…y`.

## Bloques (fuente de verdad)

| Bloque | Números | Rama / workstream | Estado |
|---|---|---|---|
| A | 000-099 | histórico (pre-bloques) | cerrado |
| B | 100-149 | `master` | en uso |
| C | 150-198 | `feature/optimize` (board/cards 179-198) + `master` (sim/engine 152-178) | **cerrado** (fusionado; no se reutiliza) |
| D | 199-299 | `feature/optimize` (trabajo nuevo) + `master` (201-207) | en uso |
| E | 300-399 | siguiente rama que cree numerados | libre |
| F | 400-499 | siguiente rama | libre |

> El rango C quedó mezclado tras la fusión (`master` 152-178, `feature/optimize` 179-198); se
> cierra y no se reutiliza. El primer número libre global es **199** (bloque D).
>
> Colisión real en el bloque D: `master` numeró `206_sprite_collision`/`207_sprite_layer` y
> `feature/optimize` ya tenía `206_message_loop`. Al fusionar `master` en la rama se renumeró la
> demo de `feature/optimize` (`206_message_loop` → `208_message_loop`), por ser la de menor
> impacto (menos referencias y solo en la rama). `master` queda con `201-207`.
>
> Al fusionar de vuelta `feature/optimize` en `master` vuelve a colisionar el `208`
> (`master` ya tiene `208_blitter_memcpy`): se renumera otra vez la demo de la rama
> (`208_message_loop` → `212_message_loop`). En tests, `master` tenía `252_copper_blitter` y la
> rama `252_os_input`: se renumera el de `master` a `260_copper_blitter`.

## Cómo reservar un bloque

1. En `master`: añadir una fila a §Bloques (`bloque X: 300-399 → rama/foo`).
2. Commit: `docs(numbering): reservar 300-399 para rama/foo`.
3. En la rama: `git merge master`; crear demos/tests con números del bloque.
4. Antes de mergear: `bash tools/run-host-tests.sh` (corre `test-numbering` y `demo-numbering`).

## Herramientas

- `node tools/check/test-numbering.mjs` — tests host: sin duplicados y catálogo (`tests/host/README.md`) sincronizado.
- `node tools/check/demo-numbering.mjs` — demos: sin duplicados por plataforma.

Ambos corren en la pasada completa de `tools/run-host-tests.sh`.
