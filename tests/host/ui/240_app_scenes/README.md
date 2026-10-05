# HOST-240: pila de escenas de la fachada de juego (`eng::App`)

Test host de los **estados de juego** de la fachada (`eng/api/api.hpp`): `App` mantiene una pila de
escenas para el flujo title→game→gameover sin que el juego baje a registros ni gestione memoria.

## Qué comprueba

1. Sin escena en la pila, `update`/`render` van al `Game` (comportamiento previo intacto).
2. `push_scene(s)` llama a `s.enter(app)` y apila la escena; la **superior conduce** `update`/`render`
   y el `Game` deja de correr mientras haya escena activa.
3. Apilar una segunda escena hace que **solo la superior** conduzca el frame.
4. `pop_scene()` llama a `exit` de la escena sacada y **revierte** a la anterior; sobre pila vacía
   devuelve `false`.
5. `set_scene(s)` vacía la pila (con `exit` de cada escena) y empuja la nueva (con `enter`).
6. Los hooks `enter`/`exit` (y `update`/`render`) son **opcionales**: se detectan con `requires`.
7. Capacidad **fija sin heap** (`kMaxScenes`); empujar por encima falla con `false`.

## Contrato

Mientras haya una escena apilada, su `update`/`render` **sustituyen** a los del `Game`; el `Game`
sigue siendo el *composition root* que empuja la primera escena. La vida de la escena la conserva el
llamador (igual que con el `Game&`). El despacho usa thunks de puntero a función (sin vtable).
