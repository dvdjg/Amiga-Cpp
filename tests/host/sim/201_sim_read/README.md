# HOST-201: lectura de tells

Test host de `engine/include/eng/sim/read.hpp`: el modelo de lectura de un observador.

## Qué comprueba

1. **Aprende en showdown**: un rival cuyas fugas coinciden siempre con mano fuerte se
   vuelve "legible" (`p_strong` alto, `TellStyle::Readable`); uno cuyas fugas aparecen con
   mano fuerte y débil a partes iguales queda "ruidoso" (`TellStyle::Noisy`, cerca del 50 %).
2. **Sin showdown no hay etiqueta**: no hay indicio ni clasificación hasta acumular
   `min_samples` muestras.
3. **Suspicacia**: descuenta los tells (el indicio se reduce con `suspicion` alta).
4. **Prior por arquetipo**: suponer un embustero baja el prior de mano fuerte respecto a
   suponer un pardillo.

## Salida de referencia

```
eng::sim read:
OK: eng::sim read (aprendizaje de tells, Bayes-lite, prior y suspicacia)
```

## Ejecutar

```bash
CXX="/c/.../g++.exe" bash tools/run-host-tests.sh tests/host/sim/201_sim_read
```
