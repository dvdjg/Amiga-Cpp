# HOST-004 · input (entrada unificada)

Test host del vocabulario de entrada portable del engine (paso 6 de
`ENGINE_DESIGN.md` §5): `eng::input::InputAggregator`, `PadState`, `MouseState`
y `KeyState`.

## Qué valida

- Los tipos son POD (trivialmente copiables, portables).
- El estado por defecto es "sin entrada".
- `PadState` agrega direcciones (pad digital) y botones CD32 (`fire`, `fire2`,
  `play`, `yellow`, `green`, `reverse`, `forward`).
- `InputAggregator::any()` agrega ambos pads, el ratón y el teclado.

## Estado

Pasa con `g++` nativo (ver `SETUP_NUEVO_EQUIPO.md` §8). El backend de lectura
(potgo/CIA/teclado) se cablea en la demo de input (pendiente).
