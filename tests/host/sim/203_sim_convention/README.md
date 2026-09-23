# HOST-203: convenciones secretas (base del Mus)

Test host de `engine/include/eng/sim/convention.hpp`: el código de gestos pactado.

## Qué comprueba

1. **Pacto**: `Convention` guarda pares gesto→señal y `lookup` los resuelve.
2. **Emisión/decodificación**: `emit_convention` produce la señal pactada y sube la
   exposición; el compañero que comparte la decodifica; quien no la comparte solo ve un
   gesto (`NotShared`); un gesto no pactado es `UnknownGesture`.
3. **Disimulo**: más disimulo ⇒ menos exposición por uso y desvanecimiento más rápido.
4. **Inferencia**: el mismo par repitiendo el mismo gesto en momentos de decisión sube la
   sospecha del observador hasta deducir la convención; fuera de una decisión no se observa.

## Salida de referencia

```
eng::sim convention:
OK: eng::sim convention (pacto, disimulo, exposicion e inferencia)
```

## Ejecutar

```bash
CXX="/c/.../g++.exe" bash tools/run-host-tests.sh tests/host/sim/203_sim_convention
```
