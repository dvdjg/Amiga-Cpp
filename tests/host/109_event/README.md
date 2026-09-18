# HOST-109: emisor de eventos de capacidad fija

Test host de `engine/include/eng/core/util/event.hpp`:
`eng::util::Event<Signature, MaxSubscribers>`, el patrón observador del engine sin heap
ni virtuals. Guarda suscriptores `FunctionRef` (no los posee) y los invoca en orden de
suscripción.

## Qué comprueba

1. **Suscripción/emit/clear**: los suscriptores se invocan y acumulan entre emisiones;
   tras `clear` un `emit` no invoca a nadie.
2. **Capacidad**: `subscribe` devuelve `false` cuando ya no caben más; `capacity()` la
   expone.
3. **Orden**: los suscriptores se invocan en orden de suscripción; eventos sin
   argumentos (`Event<void(), N>`).

## Salida de referencia

```
Event:
OK: Event (suscripcion, capacidad, clear y orden)
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/109_event
```
