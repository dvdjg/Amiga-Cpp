# HOST-138: núcleo de `eng::board`

Test host de `engine/include/eng/board/core/{types,zobrist,game}.hpp`: los tipos base
de los juegos de tablero, el generador Zobrist y el contrato de reglas.

## Qué comprueba

1. `Color`/`opposite`, empaquetado de `Piece` (color + tipo) y casilla vacía.
2. Geometría **0x88**: validez por máscara `0x88`, `file`/`rank` e índice compacto
   0..63; una casilla que se sale del tablero se detecta.
3. Empaquetado de `Move` (`from`/`to`/`payload`) y centinela `kNoMove`.
4. `Score` y `score_is_mate`; `terminal_is_over`.
5. Zobrist: secuencia determinista, no nula, y `ZobristKey` (XOR que es su propia
   inversa).
6. `GameRules`: una policy de prueba (`MockGame`) cumple el concepto y `NotGame` no
   (comprobado con `static_assert`).

## Salida de referencia

```
eng::board core:
OK: eng::board core (tipos, 0x88, moves, score, zobrist, GameRules)
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/board/138_board_core
```
