# HOST-159: rumores y memoria de grupo (`eng::sim`)

Test host de `engine/include/eng/sim/rumor.hpp`: cómo el conocimiento individual se vuelve
colectivo y mueve la reputación y la economía.

## Qué comprueba

1. **`contribute`**: aporta a la memoria de la facción solo las creencias que superan el
   umbral, con la **distorsión** del rumor; dos aportaciones del mismo rumor lo refuerzan
   (y saturan).
2. **`GroupMemory`**: `of`/`knows` por facción.
3. **`apply_group_knowledge`**: saber de un `Enemy` baja su reputación, saber de un `Ally`
   la sube, y conocer una `FoodSource` sube la demanda de comida; devuelve el cambio neto.
4. **`SimWorld::diffuse_knowledge`**: difunde entre correligionarios de la misma región,
   alimenta la memoria de grupo y aplica sus efectos sociales/económicos.

## Salida de referencia

```
Sim rumors:
OK: Sim rumors (memoria de grupo, reputacion colectiva, difusion)
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/159_sim_rumors
```
