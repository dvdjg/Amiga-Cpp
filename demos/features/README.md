# Features (demos portables)

Demos de **feature**: la lógica portable vive en el **engine** (anillo 0) y cada plataforma es un
**adaptador fino** (`main()`, backend, assets, mapping de input). Path:
`features/<feature>[/<subfeature>]/<plataforma>/NNN_<tema>/`.

- Estructura y numeración: [`../../docs/STRUCTURE.md`](../../docs/STRUCTURE.md) §4 y
  [`../../docs/ai-dev-environment/NUMBERING.md`](../../docs/ai-dev-environment/NUMBERING.md).
- Plan y decisiones: [`../../docs/guides/roadmap/PLAN_ORGANIZACION_DEMOS.md`](../../docs/guides/roadmap/PLAN_ORGANIZACION_DEMOS.md).

El **id de build/out** de una feature deriva de la **ruta** (`features/ui/amiga/007_menu` →
`ui_amiga_007_menu`), así la misma demo en varias plataformas no se machaca. La variante de build
(`A500`/`A1200`/`ST`/`STE`) va en el `CONFIG_ID`.

## Matriz de paridad (`feature × plataforma`)

| Feature | pc | amiga | a1200 | atarist | megadrive |
|---|---|---|---|---|---|
| `engine` (self-check) | — | ✅ 060 | — | — | — |
| `board/chess` | — | ✅ 123 | — | — | — |
| `cards` | — | ✅ 124 | — | — | — |
| `ui` | — | ✅ 215, 300 | — | — | — |

Un ✅ es una demo existente; `—` es pendiente. El objetivo es que cada feature tenga una variante
por plataforma con **comportamiento equivalente** (misma secuencia de decisiones → mismo resultado
lógico), que se prueba con un contrato de equivalencia en host: ver
[`PARITY_CONTRACT.md`](PARITY_CONTRACT.md).

## Reglas de una feature

- **No usa hardware directo**: nada de registros (`$dff`, `0x…`) ni vocabulario de chipset. Lo
  vigila `tools/check/demo-platform-boundaries.mjs`.
- El backend se instancia en `main()`; el adaptador puede incluir `eng/platform/<familia>/backend.hpp`
  y `input_poll.hpp` (lectura de entrada), nada más.
- La lógica reutilizable **sube al engine** si otra feature/plataforma la necesita.
