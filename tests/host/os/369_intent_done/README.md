# Test HOST-369: completación de intención por evento (`IntentDonePoster`)

El **bucle cerrado** del planner: al ejecutarse una petición de una `IntentQueue`, el `Done` (un
`IntentDonePoster`) postea un **`Msg`** `IntentDone` con su **`ticket`** al puerto del mini-SO. Ver
[`INTENT_PLANNER.md`](../../../../docs/engine/architecture/INTENT_PLANNER.md) §5.

## Respalda

| Cabecera | Qué |
|---|---|
| [engine/include/eng/os/intent_done.hpp](../../../../engine/include/eng/os/intent_done.hpp) | `IntentDonePoster<N>` — el `Done` real (handle al puerto) |
| `eng/os/message.hpp` | `MsgType::IntentDone` (+ el `ticket` en `payload.user.a`) |

## Comprueba

- Declarar una intención **no** publica evento todavía.
- Al ejecutarse (`flush`), llega un `Msg` `IntentDone` con el **ticket** correcto.

## Ejecución

```
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/os/369_intent_done
```
