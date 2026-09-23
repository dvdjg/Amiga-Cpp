# HOST-144: tabla de transposición y ordenación de jugadas

Test host de `eng/board/search/tt.hpp` y `eng/board/rules/chess/ordering.hpp`.

## Qué enseña / comprueba

- **TT de 12 bytes**: la entrada empaqueta `key u32 + score s16 + depth/flag u8 +
  reservado u8 + move u32`; `static_assert` fija el tamaño para que el presupuesto
  de RAM (`core/budget.hpp`) cuadre. Se comprueba sondeo/escritura, el flag
  (`Exact`/`Alpha`/`Beta`) y el reemplazo cuando dos claves caen en el mismo índice.
- **Ordenación MVV-LVA**: en una posición con dos capturas de dama (peón y torre) y
  una de peón, primero van las de dama y, entre ellas, el atacante más barato.
- **Killers**: una jugada tranquila que provocó un corte alfa-beta se recuerda para
  ese ply y puntúa por encima de una jugada normal.
- **Reuso**: la segunda búsqueda a la misma profundidad con la TT caliente explora
  **menos** nodos y da el mismo resultado.

## Salida de referencia

```
Ajedrez: TT y ordenacion:
OK: TT (12 B, sondeo, reemplazo, reuso) y ordenacion (MVV-LVA, killer)
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/144_chess_tt_ordering
```
