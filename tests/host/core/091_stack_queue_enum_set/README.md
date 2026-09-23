# HOST-091 — Stack/Queue/Deque y EnumSet

Respalda `engine/include/eng/core/util/stack_queue.hpp` y
`engine/include/eng/core/util/enum_set.hpp` (y el `push_front`/`pop_back` añadidos a
`ring_buffer.hpp`).

## Qué cubre

- **`Stack<T, N>`** (LIFO sobre `StaticVector`): `push`/`pop`/`top`/`clear`, rechazo al
  llenar.
- **`Queue<T, N>`** (FIFO sobre `RingBuffer`): `push`/`pop`/`front`/`back` y reciclado.
- **`Deque<T, N>`** (doble cola sobre `RingBuffer`): `push_front`/`push_back`/
  `pop_front`/`pop_back` y `operator[]` (0 = más antiguo).
- **`EnumSet<E, N>`** (bit set tipado por enum): `set`/`reset`/`flip`/`test` por valor
  del enum, `count`/`any`/`none`/`all` y `operator==`.
- **`RingBuffer` doble-ended**: `push_front`/`pop_back` directos.

## Uso previsto

Vocabulario explícito de estructuras clásicas (sin heap) y flags de estado tipados
(`EnumSet` evita índices a mano). Coste cero: son envoltorios finos.

## Ejecución

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/core/091_stack_queue_enum_set
```
