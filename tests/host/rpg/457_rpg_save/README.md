# HOST-457: serialización del núcleo RPG (`eng::rpg/save.hpp`)

Test host de `engine/include/eng/rpg/save.hpp`: la partida del núcleo se guarda y carga por componente sobre los cursores `ByteWriter`/`ByteReader` (little-endian, con comprobación de límites).

## Qué comprueba

1. **Round-trip completo**: hechos + instancias + inventario + equipo + base de atributos + reloj; los handles del inventario y del equipo **valen igual tras cargar** (slot y generación exactos, incluido un hueco reciclado: el handle destruido sigue caducado y el reciclado vivo); la derivada de stats queda pendiente de recalcular.
2. **Determinismo**: dos saves del mismo estado son byte a byte iguales, y un save después de `load_all` reproduce el original (no se serializan epoch ni cachés).
3. **Robustez**: versión distinta, stream truncado y stream vacío se rechazan con `false`; la coherencia referencial se valida con un stream manual bien formado (control positivo) y el mismo stream con un handle colgante (rechazo).

## Salida de referencia

```
RPG save (serializacion por componente):
OK: RPG save (round-trip, determinismo, robustez)
```

## Ejecutar

```bash
bash tools/run-host-tests.sh tests/host/rpg/457_rpg_save
```
