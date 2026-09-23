# HOST-156: cuerpo procedural (`eng::sim`)

Test host de `engine/include/eng/sim/body.hpp`: la representación física/expresiva de una
criatura como **cadena de chunks con IK**, alimentada por la conducta y el afecto.

## Qué comprueba

1. **`ChainBody`**: `reset` endereza la cadena; `solve` (FABRIK) lleva la punta al objetivo
   conservando la longitud de los segmentos y manteniendo la raíz fija; un objetivo fuera
   de alcance estira la cadena recta hacia él.
2. **`pose_from_behavior`**: cada conducta tiene postura base (cazar se inclina y estira,
   cortejar menea la cola, someterse se agacha).
3. **`pose_from_state`**: el afecto modula la postura — el miedo agacha y retrocede, la ira
   inclina, y un **vínculo negativo** (enemigo cercano) tensa la postura (inclina y baja la
   cabeza).
4. **`apply_pose`**: la postura deforma la cadena (agacharse baja la Y).
5. **Genérico sobre el escalar**: `double` y `q12` (con `fixed_math`), sin heap.

## Salida de referencia

```
Sim body:
OK: Sim body (cadena, IK, postura, generico double/q12)
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/sim/156_sim_body
```
