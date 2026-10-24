# HOST-490 - Física 3D arcade mínima

Verifica `eng/sim/physics3d.hpp` con `float` y `Fixed<s16,8>`: integración semi-implícita con gravedad, suelo plano, un impulso por contacto esfera-esfera, corrección posicional, reposo (`sleep`) y `time_scale` (0 congela). El test comprueba que una esfera cae y descansa en `y = radio`, que un cuerpo estático no se mueve, que dos esferas que se cruzan se separan y rebotan, y que el mismo caso funciona con fixed.

Alcance: no hay broadphase 3D, OBB/SAT, heightfield, CCD, pilas ni solver iterativo; es el primer corte de G7. El test es host puro; la validación on-target y la demo quedan pendientes.

```bash
bash tools/run-host-tests.sh tests/host/sim/490_physics3d
```
