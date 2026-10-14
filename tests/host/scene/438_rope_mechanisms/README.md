# HOST-438 - `rope_mechanisms` (cuerda/puente, muelle y catapulta, F6)

Test host de los mecanismos de escena de **F6** (`ROADMAP_SPRITES_COMPLETO.md` §3.6):
`eng/scene/rope.hpp` (cuerda/puente verlet con restricciones de distancia y contacto de
actor) y `eng/scene/mechanisms.hpp` (muelle comprimible y catapulta de carga/disparo).

Cubre, genérico sobre el escalar (`s32`/`float`; catapulta también `s16`):

- `rope_step`: los extremos anclados no se mueven, la cadena converge al catenario con la
  longitud de reposo (más rest ⇒ menos caída), gravedad y amortiguación.
- `rope_find_contact`/`rope_resolve_player`: apoyo de los pies sobre el segmento (proyección
  de la X), reparto del impulso entre los dos puntos y rebote.
- `spring_update`: contacto por ventana (X ± ancho, Y), compresión por velocidad de caída,
  liberación con boost y damper desactivado mientras está pisado.
- `catapult_load/fire/update/tip`: ángulos en unidades de tabla (0 = derecha, 64 = arriba),
  extremo exacto en 0/64/−64, disparo al cruzar el ángulo de suelta.

Es freestanding (sin hardware, sin heap, sin STL).

## Ejecución

```bash
bash tools/run-host-tests.sh tests/host/scene/438_rope_mechanisms   # solo este
bash tools/run-host-tests.sh                                       # todos
```
