# HOST-179: reglas de Go 9×9

Test host de `eng/board/rules/go/{board,movegen,rules}.hpp`.

## Qué enseña / comprueba

Go se juega sobre 81 puntos; un grupo (piedra + adyacentes del mismo color, por
*flood fill*) vive si conserva alguna libertad. Reglas validadas:

- **Tablero vacío**: 81 jugadas legales (negro mueve).
- **Captura**: al colocar la última libertad de un grupo rival, sus piedras se
  retiran y se cuentan; `unmake` restaura la posición.
- **Suicidio**: prohibido colocar una piedra cuyo grupo quede sin libertades (salvo
  si la jugada captura).
- **Ko simple**: una captura de una sola piedra que deja al grupo capturador con una
  única libertad marca el punto prohibido; recapturar de inmediato es ilegal y el ko
  se levanta al jugar en otro sitio.

La jugada codifica el punto en 8 bits y un flag de captura (`go_move`), para que la
ordenación y la quiescence no simulen.

## Salida de referencia

```
Go 9x9: reglas:
OK: Go (81 jugadas, captura, suicidio y ko simple)
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/board/179_go_rules
```
