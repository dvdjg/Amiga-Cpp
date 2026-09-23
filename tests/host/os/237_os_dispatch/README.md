# HOST-237: despacho por tabla (`eng::os`)

Test host de la tabla de despacho (`engine/include/eng/os/dispatch.hpp`): resuelve el `MsgType` como
índice en una tabla `constexpr` de handlers.

## Qué comprueba

1. `HandlerTable`: `fill` pone un default, `set` especializa por tipo, `get` consulta.
2. **Cobertura**: todos los `MsgType` tienen handler no nulo.
3. `dispatch` con `nullptr` es seguro.
4. `dispatch_all` drena el puerto y despacha cada mensaje.

## Salida de referencia

```
OK: despacho por tabla (cobertura, default, drenado) validado.
```

## Ejecutar

```bash
bash tools/run-host-tests.sh tests/host/os/237_os_dispatch
```
