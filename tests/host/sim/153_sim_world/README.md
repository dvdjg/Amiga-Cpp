# HOST-153: mundo de simulación y LOD (`eng::sim`)

Test host del contenedor del ecosistema y su modelo de nivel de detalle. Valida
`engine/include/eng/sim/world.hpp` y `engine/include/eng/sim/society.hpp`.

## Qué comprueba

1. **Población**: `spawn` asigna ids correlativos desde 1, `find` resuelve por id y
   devuelve `nullptr` para ids inválidos; al llenarse, `spawn` devuelve `no_entity`.
2. **Grafo de habitaciones**: `link_rooms` crea enlaces bidireccionales; `rooms_adjacent`,
   `room_degree` y `room_link` (con centinela `no_room`) consultan la adyacencia que usará
   el pathfinding macro off-screen.
3. **LOD realizado**: `realize_room` marca hasta `max` criaturas de la room indicada y
   deja abstractas las sobrantes sin tocar otras rooms; `realized_count` y
   `clear_realized`.
4. **Tick realizado**: avanza necesidades, envejece trackers y elige un comportamiento
   válido para la criatura viva marcada como realizada.
5. **Tick abstracto escalonado**: con `stagger_period = 4`, cada llamada procesa 1/4 de
   las criaturas (por `id % period == cursor`); un periodo completo actualiza todas una
   vez. Con un peligro ambiental activo (`set_hazard`/`set_rain`), la criatura migra por el
   grafo hacia su refugio y marca `in_den`; el hambre extrema la mata.
6. **Sociedad**: reputación por facción (`adjust` con recorte a `[-100,+100]`,
   `hostile`/`friendly`/`neutral`) y `Pack` (líder separado de los miembros, ascenso al
   irse el líder, capacidad fija).
7. **Determinismo**: dos mundos con el mismo estado inicial y la misma semilla del PRNG
   producen el mismo comportamiento y las mismas necesidades.
8. **Ciclo de vida en el mundo**: `try_reproduce` engendra una cría de una pareja madura
   (genoma heredado + coste parental) y la vejez mata a la criatura al alcanzar la edad
   límite.
9. **Puesta de la reina**: `lay_brood` hace que una reina ponga un huevo de la casta más
   necesaria (según el censo) con el genoma sesgado y registra el nacimiento en la colonia.
10. **Planificación integrada**: `replan` asigna un plan GOAP a una criatura, que se
    consulta con `current_action` y se consume con `advance_plan`; `planning_count`
    refleja los planes activos.
11. **Objetos y economía**: ejecutar el plan de refugio (`execute_action`) deja un
    `Shelter` en el mundo y consume los materiales; `offer_gift` sube la reputación del
    receptor y la demanda del objeto.
12. **Tend y conocimiento**: cuando el adulto dedica el tick a la cría (`Behavior::Tend`,
    con un `Tracker` de tipo `Kin`), le transmite su conocimiento (`transfer_knowledge`).

## Salida de referencia

```
Sim world:
OK: Sim world (poblacion, LOD, ticks, migracion, sociedad, ciclo de vida, puesta, planificacion, objetos, tend)
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/sim/153_sim_world
```
