# HOST-454: núcleo de mundo RPG — hechos y reloj (`eng::rpg`)

Test host de `engine/include/eng/rpg/{types,facts,clock}.hpp`. Es la primera pieza del **núcleo de mundo RPG** que se apoyará sobre el ecosistema de `eng::sim` (diseño: [RPG_LAYER.md](../../../../docs/engine/architecture/RPG_LAYER.md)).

## Qué comprueba

1. **`FactStore`**: la semántica de *cambio efectivo* — `epoch()` sube y el bit `changed(id)` se marca solo cuando el valor cambia de verdad (re-escribir el mismo valor no reevalúa quests); `add` satura a `0xffff` sin envolver; ids fuera de rango devuelven `false`/0 en vez de tocar memoria; `present_count`, `for_each_present` (orden ascendente, lo que fija el formato de save) y `clear` total (epoch a 0).
2. **`FactSet`**: hechos pegados a una entidad — pertenencia sin duplicados, capacidad finita con rechazo y hueco reutilizado tras `remove`.
3. **`WorldClock`**: acarreo minuto → hora → día por división nativa (`divmod32`/`divu_w`, sin libcalls de 32 bits), deltas grandes, `advance_hours`/`advance_days` (que conserva la hora), saturación del día a `kLastDay`, `set_time` con recorte al rango válido y `minutes_of_day`.

## Salida de referencia

```
RPG kernel (hechos, reloj de mundo):
OK: RPG kernel (FactStore/FactSet/WorldClock)
```

## Ejecutar

```bash
bash tools/run-host-tests.sh tests/host/rpg/454_rpg_kernel
```
