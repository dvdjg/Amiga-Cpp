# HOST-108: máquina de estados finita genérica

Test host de `engine/include/eng/core/util/state_machine.hpp`:
`eng::util::StateMachine<State, Event>` con tabla de transiciones `constexpr` externa
(sin heap, sin virtuals). Es el motor genérico de decisión del engine y la base sobre la
que se apoya `eng::ai::decision` (ver
[`GAME_AI_LIBRARY.md`](../../../docs/engine/architecture/GAME_AI_LIBRARY.md)).

## Qué comprueba

1. **Semáforo**: ciclo `Rojo → Verde → Ámbar → Rojo` con el evento `Paso`; `reset()`
   vuelve al estado inicial y reinicia el contador.
2. **FSM de IA de un guardia** (`Patrulla`/`Alerta`/`Persecución`/`Recarga`): transiciones
   válidas, **evento sin transición** (no cambia de estado y `dispatch` devuelve `false`),
   `reset(estado)` a un estado concreto y contador de transiciones.
3. **Orden de la tabla**: gana la **primera** transición que coincide `(estado, evento)`.

## Salida de referencia

```
FSM:
OK: StateMachine (semaforo, guardia de IA, orden de tabla)
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/108_state_machine
```
