# HOST-236: prioridad, peek, coalescing y VBlank latched

Test host de la cola con prioridad y el VBlank latched (`engine/include/eng/os/port.hpp`).

## Qué comprueba

1. `PrioMsgQueue`: los `High` **se cuelan** ante `Normal`/`Low`; `peek` mira sin retirar;
   `has_at_least` responde por prioridad; `push_mouse_coalesced` deja solo el último `MouseMove`.
2. `VBlankLatch`: la **secuencia avanza** aunque no se consuma, hay **como máximo uno pendiente** y
   `missed` cuenta los pisados; `take_vblank` los devuelve y se resetea.

## Salida de referencia

```
OK: prioridad, peek, coalescing y VBlank latched validados.
```

## Ejecutar

```bash
bash tools/run-host-tests.sh tests/host/236_os_latched
```
