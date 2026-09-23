# HOST-113: árboles de comportamiento

Test host de `engine/include/eng/ai/decision/behavior_tree.hpp`:
`eng::ai::BehaviorTree<MaxNodes>` con nodos en un array de capacidad fija y hojas
`FunctionRef<BtStatus()>` (sin heap). Soporta **secuencia** y **selector**.

## Qué comprueba

1. **Secuencia**: falla en el primer hijo que falla (cortocircuito, no evalúa el resto).
2. **Selector**: tiene éxito en el primer hijo que lo logra.
3. **Casos límite**: árbol vacío/sin raíz y selector con todos los hijos fallando.
4. **Caso de uso**: guardia que dispara si tiene munición o recarga si no.
5. **Capacidad**: `add_*` devuelve `no_node` cuando el array está lleno.

Sin estado `Running`: el tick es síncrono y acotado.

## Salida de referencia

```
BehaviorTree:
OK: BehaviorTree (secuencia, selector, cortocircuito, capacidad)
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/ai/113_behavior_tree
```
