# HOST-006 · input_decode (joystick y ratón)

Test host de las piezas puras del backend de entrada: `eng::amiga::decode_joystick` (bits Gray de `JOYxDAT` a direcciones) y `eng::amiga::mouse_counter_delta` (delta signed del contador del ratón con wrap de 8 bits, AHRM cap. 8).

## Qué valida

- Reposo sin direcciones.
- Cada dirección pura (derecha/izquierda/arriba/abajo) y el XOR que distingue
  arriba/abajo de las diagonales.
- Diagonales (arriba+derecha).
- Que `JOYxDAT` no contiene el fuego (lo gestiona `CIAAPRA` aparte).
- Deltas de ratón positivos/negativos, los wraps `0xFF→0x00` y `0x00→0xFF`, y los límites `+127`/`-128`.

## Estado

Pasa con `g++` nativo. Las lecturas de hardware (`poll_input`/`poll_mouse`) y el mapeo a `InputAggregator` se ejercen en las demos de entrada.
