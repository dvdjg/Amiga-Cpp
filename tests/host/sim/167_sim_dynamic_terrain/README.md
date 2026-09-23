# HOST-167: terreno dinámico (`eng::sim`)

Test host de los **eventos de terreno** de `SimWorld` y su interacción con el clima.

## Qué comprueba

1. **Inundación**: convierte la región en agua y trae clima de inundación; cambia la
   travesía (`region_passable` con `swim` sí, con `walk` no) y solo afecta a su región.
2. **Incendio** → zona peligrosa + calor; **derrumbe** → escombros + polvo.
3. **Regeneración** → cobertura y limpia el clima.
4. **Parametrización** (`TerrainEventParams`) y nombres/mapeos legibles.

## Salida de referencia

```
Sim dynamic terrain:
OK: Sim dynamic terrain (inundacion, incendio, derrumbe, regeneracion)
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/sim/167_sim_dynamic_terrain
```
