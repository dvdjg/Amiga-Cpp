# HOST-174: jugador simulado y LOD (`eng::sim`)

Test host de la capa que hace **jugable** el ecosistema y gobierna su coste: un avatar con
IA como observador, el **nivel de detalle** por distancia, el **despertar gradual** y el
**aforo dinámico**.

## Qué comprueba

1. **`lod.hpp`**: `band_for` reparte `Realized`/`Abstract`/`Dormant` por distancia y
   `lod_costs_cpu` marca que lo dormido no cuesta.
2. **`SimWorld::update_lod`**: realized cerca del jugador, abstract a media distancia y
   **dormant** lejos; el dormido **no se simula** y vuelve a realized al acercarse.
3. **Despertar gradual**: `wake_per_frame` limita cuántas criaturas se realizan por frame y
   `lod_blend` sube por pasos; la criatura no decide hasta completar la transición (sin
   "pop" ni pico de trabajo).
4. **Aforo dinámico** (`season.hpp`): la capacidad es la del bioma modulada por **estación**
   (invierno sostiene menos) y **clima** (tormenta extrema la mitad).
5. **Jugador simulado** (`avatar.hpp`): `intent_for`, `player_step` y `player_view`
   (resumen de lo que percibe y de la carga).

## Salida de referencia

```
Sim player lod:
OK: Sim player lod (bandas, wake, aforo dinamico, avatar, percepcion)
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/sim/174_sim_player_lod
```
