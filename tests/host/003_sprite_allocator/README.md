# HOST-003 — `eng::graphics::SpriteAllocator`

Test host del asignador de canales de sprite hardware (paso 4 de
`ENGINE_DESIGN.md` §5). Valida la lógica pura de reparto de `SpriteIntent` entre
los 8 canales con multiplexado vertical (greedy first-fit) y la decisión de
overflow → BOB.

Cubre:

- `multiplexado vertical`: sprites sin solape vertical comparten canal.
- `overflow horizontal`: más de 8 sprites solapados desbordan a `as_bob`.
- `reuso`: sprites que no solapan reutilizan canales libres entre franjas.
- `límite`: el canal queda libre justo cuando `busy_until < top`.

Es freestanding (sin hardware, sin heap): se compila con `g++` del host y corre
como binario nativo. El `main.cpp` usa `printf` solo para informar.

## Ejecución

```bash
bash tools/run-host-tests.sh tests/host/003_sprite_allocator   # solo este
bash tools/run-host-tests.sh                                    # todos
```
