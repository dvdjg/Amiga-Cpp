# HOST-003 — `eng::graphics::SpriteAllocator`

Test host del asignador de canales de sprite hardware (paso 4 de
`ENGINE_DESIGN.md` §5). Valida la lógica pura de reparto de `SpriteIntent` entre
los 8 canales con multiplexado vertical (greedy first-fit) y la decisión de
overflow → BOB.

Cubre:

- `multiplexado vertical`: sprites sin solape vertical comparten canal.
- `overflow horizontal`: más de 8 sprites solapados desbordan a `as_bob`.
- `reuso`: sprites que no solapan reutilizan canales libres entre franjas.
- `límite`: el canal queda libre justo cuando termina el tramo anterior (ocupación exacta
  por línea: bitfield de 256 líneas por canal).
- `tiras horizontales`: corrida contigua o la tira entera a BOB.
- `par attached`: el par va en un canal par, el impar en el contiguo.
- `cadena vertical`: todas las franjas del mismo objeto al mismo canal (o entera a BOB).
- `canal preferido`: el `channel` del intent se respeta si está libre.
- `prioridad de asignación`: `assign_rank` (fijos/grupos antes que libres).
- `grupo con trayectoria`: corrida contigua para el bounding box del grupo (o entera a BOB).

Es freestanding (sin hardware, sin heap): se compila con `g++` del host y corre
como binario nativo. El `main.cpp` usa `printf` solo para informar.

## Ejecución

```bash
bash tools/run-host-tests.sh tests/host/graphics/003_sprite_allocator   # solo este
bash tools/run-host-tests.sh                                    # todos
```
