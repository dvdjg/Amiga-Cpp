# HOST-142: notación SAN y UCI

Test host de `eng/board/rules/chess/notation.hpp`.

## Qué enseña / comprueba

SAN (Standard Algebraic Notation), la notación que usan libros, tablas y comentarios:

| Caso | Salida | Regla |
|---|---|---|
| peón a e4 | `e4` | el peón no lleva letra |
| caballo a f3 | `Nf3` | inicial de la figura |
| exd5 | `exd5` | peón captura: columna de origen + `x` |
| a8=Q | `a8=Q` | promoción |
| Nab3 / Ncb3 | desambiguación | dos piezas iguales al mismo destino: columna (o fila) |
| O-O / O-O-O | enroque | corto / largo |
| Qh4# | mate | sufijo `#` (jaque simple: `+`) |

También valida `to_uci` (`e2e4`, `a7a8q`). El sufijo `+`/`#` se calcula aplicando la
jugada y comprobando la respuesta, por eso el test usa posiciones reales.

## Salida de referencia

```
Ajedrez: notacion:
OK: ajedrez (SAN, enroque, captura, promocion, desambiguacion, mate, UCI)
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/142_chess_notation
```
