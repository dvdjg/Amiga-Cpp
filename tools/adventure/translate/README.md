# Traducción de guiones (`translate/`) — hito I1

Convierte los guiones descompilados (descumm) en **AST JSON** (`adv-ast/1`) para que el traductor a C++ de `eng::adv` trabaje sobre estructura, no sobre texto. Plan y criterio de cierre en [`ROADMAP_ADVENTURE_IMPORT.md`](../../../docs/guides/roadmap/ROADMAP_ADVENTURE_IMPORT.md) (I1); formato del AST, en `parse.mjs`/`descumm.mjs`.

## Estado

| Pieza | Estado |
|---|---|
| **Parser descumm → AST** (`descumm.mjs` + `parse.mjs`) | **HECHO** — cobertura del corpus: **MI1 92.9 %** (39 544/42 555 sentencias) y **MI2 91.5 %** (61 956/67 696); el resto son `kind:"raw"` casi todos por líneas mal formadas del propio descumm (`drawObject(509setXY(,…)`), con `error` registrado en el AST |
| **Traductor AST → C++ de `eng::adv`** | **PENDIENTE** — reglas por opcode/familia + emisión de funciones/tablas; el corpus y la cobertura ya están |

## Gramática cubierta (observada en el corpus real)

- Sentencias: `if (…) {` · `} else {` · `}` · asignaciones (`=`, `+=`, `-=`, `*=`, `/=`, `&=`, `|=`, `^=`) · `Exprmode` · post-inc/dec (`x++`, `x--`) · `goto hex` · `unless (…) goto hex` · comentarios `/* … */` (anotaciones de descumm) · `breakHere`/`stopObjectCode` (como llamadas).
- Expresiones: números, cadenas con escapes, `VAR_*`/nombres, `Local[n]`/`Var[n]`/`Bit[n]`, arrays, llamadas anidadas (`ActorOps(7,[Init(),Costume(75),…])`), operadores por precedencia, y la asignación inline `(<VAR_RESULT = f()> / 2)`.
- Nodos: `call`, `assign` (`aop` = operador; `op` = byte de opcode), `if` (`then`/`else`), `goto`, `unless-goto`, `incdec`, `comment`, `expr`, `raw` (con `error`).

## Uso

```bash
# AST de todos los guiones del workspace (+ reports/parse-coverage.json y READMEs)
node tools/adventure/translate/parse.mjs mi1

# Solo una sala / otro destino
node tools/adventure/translate/parse.mjs mi1 --room 4 --out out/adventure/mi1/decoded/ast

# Tests (gramática representativa + casos límite)
node --test tools/adventure/translate/parse.test.mjs
```

## Reglas

- La **cobertura es honesta**: lo no parseado queda como `raw` con el motivo (`error`), nunca se descarta en silencio. Subir cobertura = añadir reglas reales, no relajar el parser.
- El AST conserva `addr` y el **byte de opcode** de cada sentencia: el traductor puede contrastar contra el original en ScummVM.
- Los `.txt`/`.ast.json` son artefactos (viven en `out/`, gitignored); lo que se versiona es el parser, los tests y esta ficha.
