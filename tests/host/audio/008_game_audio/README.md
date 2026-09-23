# HOST-008 · game_audio (banco de muestras + política de voces)

Test host de la parte pura de la capa de audio de juego (`eng/audio/sfx_bank.hpp`):
`SampleBank` (catálogo de sonidos) y `allow_trigger` (política de cooldown +
límite de instancias).

## Qué valida

- `SampleBank`: registrar y buscar por `id`, ids inexistentes o fuera de rango.
- `allow_trigger`: primer disparo siempre permitido, cooldown (bloquea disparos
  cercanos), y límite de instancias simultáneas (0 = sin límite).

## Estado

Pasa con `g++` nativo. La orquestación con el mixer Amiga (`GameAudio`, en
`game_audio.hpp`) se valida en la demo 062.
