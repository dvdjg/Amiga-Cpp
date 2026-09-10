# HOST-009 · wave_tables (tablas de forma de onda)

Test host de las tablas de onda del engine (`eng/audio/wave_tables.hpp`): seno,
triangular y cuadrada 8-bit con signo (enteras, sin float, para el engine
freestanding).

## Qué valida

- Un ciclo de 64 muestras cubre el rango ±127 con media ~0.
- El seno pasa por 0 en los cuartos del ciclo y alcanza ±127 en pico/valle.

## Estado

Pasa con `g++` nativo. Es el bloque base del procedimiento de depuración de
sonido (`docs/debugging/AUDIO_DEBUG.md`).
