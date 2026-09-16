# HOST-022: rotozoom por píxel

Test host de `eng/graphics/effects/rotozoom.hpp`: valida el muestreo del rotozoom con
aritmética entera, sin hardware ni emulador.

## Qué comprueba

1. **Tabla de seno 16.16** exacta en los cuartos (`0`, `1.0`, `0`, `-1.0`).
2. **Identidad** (ángulo 0, zoom 1.0, offset = centro): el buffer de salida reproduce
   la textura periódica 1:1, byte a byte, en 160×128 píxeles.
3. **Rotación 180°**: `r = -(dx,dy)` → invierte las coordenadas de textura.
4. **Zoom 2×**: el paso por píxel se duplica.

Las tres fases se comparan contra la verdad de campo calculada en el propio test, así
que un desajuste de signos, de redondeo o de periodicidad falla de forma determinista.

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/022_rotozoom
```
