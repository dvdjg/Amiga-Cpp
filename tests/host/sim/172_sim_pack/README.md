# HOST-172: coordinación de manadas (`eng::sim`)

Test host de `engine/include/eng/sim/pack.hpp`: roles y tácticas emergentes guiadas por
señales y jerarquía.

## Qué comprueba

1. **`pack_role_for`**: liderazgo y roles rotatorios (flanqueador, seguidor, explorador).
2. **`flank_goal`**: puntos de flanqueo; los flanqueadores se reparten a **ambos lados**
   para envolver en vez de amontonarse.
3. **`SimWorld::coordinate_packs`**: el líder con presa percibida orienta a sus miembros
   (relaciones `Pack` hacia él) a posiciones de flanqueo, los pone a cazar y comparte el
   objetivo; quien no es del grupo no se coordina.

## Salida de referencia

```
Sim pack:
OK: Sim pack (roles, flanqueo, coordinacion)
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/172_sim_pack
```
