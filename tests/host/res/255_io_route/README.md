# HOST-255: E/S asíncrona y enrutado por tag

Test host del contrato de E/S asíncrona (`engine/include/eng/os/file.hpp`) y de la fachada de
recursos (`engine/include/eng/res/resources.hpp`).

## Qué comprueba

1. `IoUser` (cookie de E/S): `encode`/`decode` conservan `tag` + `id`.
2. `route_io` entrega cada `FileDone`/`FileError` al subsistema de su `tag`: `'A'` → caché de
   assets, `'L'` → loader de código; **no cruza** consumidores.
3. Un tag desconocido (stream) y los mensajes que no son de E/S **no** los consume la fachada.

## Salida de referencia

```
OK: E/S asincrona (IoUser) y enrutado por tag validados.
```

## Ejecutar

```bash
bash tools/run-host-tests.sh tests/host/255_io_route
```
