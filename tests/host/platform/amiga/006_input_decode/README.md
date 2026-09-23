# HOST-006 · input_decode (decodificación del joystick)

Test host de la pieza pura del backend de entrada: `eng::amiga::decode_joystick`
(convierte los bits del contador `JOYxDAT` de Denise en direcciones, AHRM cap. 8).

## Qué valida

- Reposo sin direcciones.
- Cada dirección pura (derecha/izquierda/arriba/abajo) y el XOR que distingue
  arriba/abajo de las diagonales.
- Diagonales (arriba+derecha).
- Que `JOYxDAT` no contiene el fuego (lo gestiona `CIAAPRA` aparte).

## Estado

Pasa con `g++` nativo. La lectura de hardware (`poll_input`) y el mapeo a
`InputAggregator` se validan en la demo 056.
