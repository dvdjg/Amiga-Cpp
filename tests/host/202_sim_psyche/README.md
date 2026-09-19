# HOST-202: estado psicológico y evolución de partida

Test host de `engine/include/eng/sim/psyche.hpp`: el estado temporal de la psique.

## Qué comprueba

1. **Estado inicial** derivado del carácter (compostura, confianza, ánimo) y sin tilt.
2. **Eventos de mesa**: un *bad beat* da tilt e ira, y el tilt se disipa con el tiempo.
3. **Comparativa de carácter**: ante la misma racha de derrotas, el irascible se le nota
   más (más fuga) que el flemático.
4. **Confianza y racha**: ganar sube confianza y racha; perder la baja.
5. **Modificador de decisión**: estado neutro = 0; el tilt sube la agresión; poca
   confianza y fatiga la bajan.

## Salida de referencia

```
eng::sim psyche:
OK: eng::sim psyche (eventos, tilt, confianza, racha y modificador)
```

## Ejecutar

```bash
CXX="/c/.../g++.exe" bash tools/run-host-tests.sh tests/host/202_sim_psyche
```
