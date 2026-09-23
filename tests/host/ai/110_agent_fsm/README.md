# HOST-110: FSM de agente con efectos de entrada/salida

Test host de `engine/include/eng/ai/decision/agent_fsm.hpp`:
`eng::ai::AgentFsm<State, Event, MaxStates>` **contiene** `eng::util::StateMachine`
(no duplica transiciones) y añade los efectos que el agente ejecuta al entrar y al salir
de cada estado (animación, objetivo, sonido, blackboard). Modelo en
[`GAME_AI_LIBRARY.md`](../../../../docs/engine/architecture/GAME_AI_LIBRARY.md).

## Qué comprueba

1. **Efectos**: un evento con transición ejecuta la salida del estado actual y la entrada
   del nuevo, en ese orden (`AaP`…).
2. **Evento sin transición**: no toca efectos y `dispatch` devuelve `false`.
3. **Estados sin efecto**: no fallan (no hay que registrar todos).
4. **`previous()`** recuerda el estado anterior y el contador de transiciones cuadra.
5. **Reemplazo**: registrar dos veces un efecto deja el último.

Los efectos se registran como funciones libres (vía `FunctionRef`), que deben vivir más
que la FSM.

## Salida de referencia

```
AgentFsm:
OK: AgentFsm (efectos de entrada/salida, sin transicion, reemplazo)
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/ai/110_agent_fsm
```
