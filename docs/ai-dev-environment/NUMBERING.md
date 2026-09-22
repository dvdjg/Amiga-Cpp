# Numeración por rama (anti-solape de demos y tests)

Las demos (`demos/<plataforma>/NNN_<tema>/`) y los tests host (`tests/host/NNN_<nombre>/`) usan
un número `NNN` **único y no reutilizable**. Con varias ramas activas a la vez, dos ramas pueden
elegir el mismo número y el merge produce duplicados (caso real: `feature/optimize` numeró
`152-171` para board/cards y `master` `152-178` para sim/engine). Para que no vuelva a pasar, la
numeración se reparte en **bloques reservados por rama/workstream**, con este documento como
**fuente única de verdad**.

## Regla

1. **Bloques de 100 disjuntos**. Cada rama/workstream que cree demos o tests numerados
   **reserva un bloque contiguo** antes de crear nada. Los números de un bloque se usan **solo**
   en su rama y **un bloque no se comparte entre ramas** (compartirlo es lo que provoca las
   colisiones). Dos ramas activas nunca numeran dentro del mismo bloque.
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
| D | 199-299 | `master` | en uso |
| E | 300-399 | `feature/optimize` (trabajo nuevo) | en uso |
| F | 400-499 | siguiente rama que cree numerados | libre |

> El rango C quedó mezclado tras la fusión (`master` 152-178, `feature/optimize` 179-198); se
> cierra y no se reutiliza. Los bloques **D y E están separados**: `master` numera en el rango
> **199-299** y `feature/optimize` en el **300-399**, así que ya no pueden chocar al fusionar.
>
> Colisión real en el bloque D (antes de separar los bloques): `master` numeró
> `206_sprite_collision`/`207_sprite_layer` y `feature/optimize` ya tenía `206_message_loop`. Al
> fusionar `master` en la rama se renumeró la demo de `feature/optimize`
> (`206_message_loop` → `208_message_loop`), por ser la de menor impacto (menos referencias y solo
> en la rama). `master` queda con `201-207`.
>
> Al fusionar de vuelta `feature/optimize` en `master` volvió a colisionar el `208` (`master` ya
> tenía `208_blitter_memcpy`): se renumeró otra vez la demo de la rama
> (`208_message_loop` → `212_message_loop`). En tests, `master` tenía `252_copper_blitter` y la
> rama `252_os_input`: se renumeró el de `master` a `260_copper_blitter`.
>
> Última colisión (D compartido, 2026-09): `master` creó `261_fine_scroll` y `feature/optimize`
> `261_ui_msg_input`. Se resolvió renumerando el de `master` a **`267_fine_scroll`**. Con los
> bloques ya separados (D para `master`, E para la rama) esta situación no puede repetirse: los
> números nuevos de `feature/optimize` van a partir de **300**.

## Cómo reservar un bloque

1. En `master`: añadir una fila a §Bloques (`bloque X: 400-499 → rama/foo`).
2. Commit: `docs(numbering): reservar 400-499 para rama/foo`.
3. En la rama: `git merge master`; crear demos/tests con números del bloque.
4. Antes de mergear: `bash tools/run-host-tests.sh` (corre `test-numbering` y `demo-numbering`).

Para saber el **siguiente número libre** de tu bloque sin contar a mano:

```
node tools/check/next-number.mjs            # usa la rama git actual
node tools/check/next-number.mjs master     # o forzar una rama/workstream
```

Imprime el bloque asignado a esa rama y el menor número libre dentro de él (mirando
`tests/host/NNN_*` y `demos/*/NNN_*`).

## Herramientas

- `node tools/check/test-numbering.mjs` — tests host: sin duplicados y catálogo (`tests/host/README.md`) sincronizado.
- `node tools/check/demo-numbering.mjs` — demos: sin duplicados por plataforma.
- `node tools/check/next-number.mjs` — siguiente número libre en el bloque de la rama.

Los dos primeros corren en la pasada completa de `tools/run-host-tests.sh`; el tercero es de apoyo.
