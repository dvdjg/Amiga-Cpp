# HOST-404 — peticiones con generación (R6.2)

`eng/os/request.hpp` (`eng::os::RequestTable` + `RequestId`): identidad de una petición de E/S
separada del `IoUser`, con **generación** por slot para **rechazar respuestas tardías** de un slot
reutilizado. Puro (sin E/S).

## Qué comprueba

- Ciclo `acquire` → `complete` (y complete duplicado → rechazado).
- **Respuesta tardía**: tras reutilizar un slot (generación distinta), un `complete` de la petición
  antigua **no** cierra la nueva.
- `cancel` de una petición en vuelo (y una respuesta posterior → rechazada).
- Tabla llena → `acquire` inválido.
