# HOST-185: GOAP numérico cuantizado

Test host de `engine/include/eng/ai/planning/numeric_goap.hpp`.

## Qué enseña / comprueba

Extiende el GOAP booleano con variables de nivel (`u8`, 0..255): los enteros son niveles
directos y los **decimales**, niveles escalados (p. ej. escala 4 = 0.25 por nivel). Hasta 4
variables la clave es exacta de 64 bits (32 de hechos + 32 de niveles); de 5 a 8 pasa a
**ancha** (`StateKeyNVWide<2>`, sin `long long`).

- Plan con precondiciones/efectos numéricos (`var_ge`, `add`, `set_var`): juntar
  madera, fabricar herramienta y minar oro; el test comprueba las 6 acciones y el
  coste 7, y que al ejecutarlo se cumple el objetivo.
- **Decimales por escala** (aceite en niveles de 0.25).
- **Saturación** de niveles (no desborda 255 ni baja de 0) y round-trip de la clave.
- **Caché**: `plan_cached` repite la consulta sin buscar (`expansions == 0`) y
  `plan_reusing` reutiliza el sufijo del plan tras ejecutar un paso.
- **Clave ancha**: `Goap<32,8>` (8 variables) hace round-trip de la clave y planifica con la
  variable 7.

## Salida de referencia

```
GOAP numerico:
OK: GOAP numerico (enteros, decimales, saturacion, memo y sufijo)
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/ai/185_goap_numeric
```
