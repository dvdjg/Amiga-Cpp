# HOST-169: biomas y ecosistemas (`eng::sim`)

Test host de `engine/include/eng/sim/biome.hpp`: la capa que combina **terreno + clima +
especies** por región.

## Qué comprueba

1. **`biome_profile`**: cada bioma define terreno dominante, peligro climático típico,
   abrigo, peligro y abundancia de comida; `biome_name`/`biome_food`.
2. **`species_fits_biome`**: una especie solo habita un bioma si puede moverse por su
   terreno (nadar en la ciénaga, trepar en la montaña).
3. **`biome_species`**: lista de especies típicas (vacía por defecto).
4. **`SimWorld::set_biome`/`apply_biome`**: vuelca el perfil en la región (terreno, abrigo,
   peligro) y, si se pide, **siembra su clima típico**; cambia la travesía.

## Salida de referencia

```
Sim biome:
OK: Sim biome (perfiles, especies, volcado al mundo)
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/169_sim_biome
```
