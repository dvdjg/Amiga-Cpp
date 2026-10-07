# HOST-009 · wave_tables (tablas de forma de onda)

Test host de las tablas de onda del engine (`eng/audio/wave_tables.hpp`): seno
genérico sobre el tipo de muestra (`sine_wave<T>`/`wave_traits<T>`), triangular y
cuadrada 8-bit con signo (enteras, sin float, para el engine freestanding).

## Qué valida

- Un ciclo de 64 muestras cubre el rango ±127 con media ~0.
- El seno pasa por 0 en los cuartos del ciclo y alcanza ±127 en pico/valle.
- La tabla generada en compilación reproduce **byte a byte** la histórica (17 valores del
  cuarto de onda).
- `sine_wave<s16>` usa el pico completo del formato (32767).
- `synth_tone<T>` escribe el tipo pedido (u8 de Paula con signo interpretado, s16 PCM) con
  la amplitud solicitada y fase 0 en la muestra inicial.

## Estado

Pasa con `g++` nativo. Es el bloque base del procedimiento de depuración de
sonido (`docs/debugging/investigaciones/audio-debug.md`).
