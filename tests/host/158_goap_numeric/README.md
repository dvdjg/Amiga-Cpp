# HOST-158: GOAP numérico cuantizado

Test host de `engine/include/eng/ai/planning/numeric_goap.hpp`.

## Qué enseña / comprueba

Extiende el GOAP booleano con hasta **4 variables de nivel** (`u8`, 0..255): los
enteros son niveles directos y los **decimales**, niveles escalados (p. ej. escala 4 =
0.25 por nivel). La clave sigue siendo exacta (32 bits de hechos + 32 de niveles).

- Plan con precondiciones/efectos numéricos (`var_ge`, `add`, `set_var`): juntar
  madera, fabricar herramienta y minar oro; el test comprueba las 6 acciones y el
  coste 7, y que al ejecutarlo se cumple el objetivo.
- **Decimales por escala** (aceite en niveles de 0.25).
- **Saturación** de niveles (no desborda 255 ni baja de 0) y round-trip de la clave.
- **Caché**: `plan_cached` repite la consulta sin buscar (`expansions == 0`) y
  `plan_reusing` reutiliza el sufijo del plan tras ejecutar un paso.

## Salida de referencia

```
GOAP numerico:
OK: GOAP numerico (enteros, decimales, saturacion, memo y sufijo)
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/158_goap_numeric
```
