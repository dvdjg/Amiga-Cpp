# Test HOST-368: `eng::graphics::IntentQueue` (intención no bloqueante + completación)

El frente común de lo asíncrono del engine: **declarar una intención ENCOLA** (no ejecuta, no
bloquea), `flush()` avanza sin esperar, `wait(ticket)` es el **único** bloqueo, y al ejecutarse
cada petición se **avisa** por la política `Done`. Ver
[`INTENT_PLANNER.md`](../../../../docs/engine/architecture/INTENT_PLANNER.md).

## Respalda

| Cabecera | Qué |
|---|---|
| [engine/include/eng/graphics/intent_queue.hpp](../../../../engine/include/eng/graphics/intent_queue.hpp) | `IntentQueue<N, Item, Executor, Done>`, `Ticket`, `DrawIntent`/`DrawQueue` |

## Comprueba

- **Declarar no ejecuta** (no bloquea): `enqueue` solo encola y devuelve un `Ticket` creciente.
- `flush()` ejecuta la cola **sin esperar** y **avisa por ticket** (`Done`).
- `wait(t)` espera —solo si se pide— a que la petición `t` se haya ejecutado.

**Nota de diseño**: el `Done` se **copia** en la cola, así que debe ser un **handle** (referencia a
un posteador/puerto), no un valor con estado propio.

## Ejecución

```
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/graphics/368_intent_queue
```
