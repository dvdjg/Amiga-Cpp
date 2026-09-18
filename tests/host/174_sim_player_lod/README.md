# HOST-174: jugador simulado y LOD (`eng::sim`)

Test host de la capa que hace **jugable** el ecosistema y gobierna su coste: un avatar con
IA como observador, el **nivel de detalle** por distancia y el **aforo por región**.

## Qué comprueba

1. **`lod.hpp`**: `band_for` reparte `Realized`/`Abstract`/`Dormant` por distancia y
   `lod_costs_cpu` marca que lo dormido no cuesta.
2. **`SimWorld::update_lod`**: realized cerca del jugador, abstract a media distancia y
   **dormant** lejos; el dormido **no se simula** (su hambre no avanza en el tick abstracto)
   y vuelve a realized al acercarse el jugador.
3. **Aforo por región** (`region_capacity`/`region_population`/`region_has_space`): la
   capacidad la fija el bioma; la región se llena y deja de aceptar (frena la reproducción).
4. **Jugador simulado** (`avatar.hpp`): `intent_for` decide por necesidades (comer, refugio,
   descansar); `player_step` mueve y come; `player_view` resume lo que percibe y la carga
   (realized/abstract/dormant) para depurar que el mundo se siente rico cerca y barato lejos.

## Salida de referencia

```
Sim player lod:
OK: Sim player lod (bandas, aforo, avatar, percepcion)
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/174_sim_player_lod
```
