# HOST-160: percepción multimodal (`eng::sim`)

Test host de `engine/include/eng/sim/senses.hpp`: cómo una criatura percibe el entorno por
varios sentidos a la vez, de forma agnóstica de la proyección.

## Qué comprueba

1. **Sensores por especie** (`senses_from_species`): visión/oído del `Species`; olfato y
   agudeza derivados del tamaño.
2. **Geometría sin trigonometría**: `sector_of`/`sector_distance` (brújula de 8 sectores) e
   `in_cone` (cono frontal; sin orientación = 360°), y `attenuation` (caída lineal).
3. **`perceive`**:
   - lo visible y **delante** se ve; lo que está **detrás** se oye pero no se ve;
   - el **oído** cruza a la región contigua, atenuado por la penalización de muro;
   - lo **invisible y silencioso** no se percibe; lo que está fuera de alcance tampoco;
   - la **novedad** (no estar en la memoria de largo plazo) sube la `salience`; si ya se
     conoce, no hay bonus.
   - `modalities` acumula todos los sentidos que dispararon y `sense` es el dominante.

## Salida de referencia

```
Sim senses:
OK: Sim senses (geometria, sentidos, novedad)
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/160_sim_senses
```
