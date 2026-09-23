# HOST-005 · audio (mezclador de Paula)

Test host de la lógica de asignación de canales del mezclador de audio (paso 7
de `ENGINE_DESIGN.md` §5): `eng::audio::SampleEvent`, `MusicEvent`, `AudioPlan`
y `AudioMixer`.

## Qué valida

- El plan por defecto no tiene canales activos.
- `play()` asigna canales por orden (first-fit).
- Con los 4 canales ocupados, un quinto sfx se rechaza.
- `channel_hint` se respeta cuando es válido.
- `begin_frame()` limpia el plan al inicio del frame.

## Estado

Pasa con `g++` nativo. La lógica es pura (host-testable); el backend materializa
el `AudioPlan` en registros Paula (`AUDxLCH/LCL/LEN/PER/VOL`). El `MusicPlayer`
(envoltorio de los reproductores asm p61/pt/ahx) está pendiente de importar desde
`demoscene-repo-orig`.
