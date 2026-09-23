# HOST-235: inventario de hardware `eng::hw`

Test host de la parte **pura** del **inventario de hardware** (`eng/hw/info.hpp`): consultas de
capacidad, helpers de display, nombres legibles y las heuristicas (modelo de maquina, clasificacion
de RAM, RTC). No llama a `probe()` (que vive en el backend y sondea hardware real; se verifica en
la demo 205 sobre A500).

## Que comprueba

1. `cpu_at_least`: orden de la familia 68000/010/020/030/040/060, umbral `Unknown` y `Other`.
2. Capacidades: `has_fast_ram`, `is_aga`, `is_ecs`, `is_cd32` (modelo o Akiko), `can_hires`.
3. Planos/colores: OCS/ECS = 6 planos / 64 colores; AGA = 8 planos / 256 colores.
4. `set_display`: tamano, flags, `max_colors` para indexado, HAM6 (4096), HAM8 (262144) y EHB (64).
5. Nombres `cpu_name`/`chipset_name`/`model_name` (y `?` para desconocido).
6. `guess_model`: OCS->A500, ECS->A600, AGA 2MB->A1200, AGA >2MB->A4000, Akiko->CD32.
7. `classify_region`: chip, `MEMF_FAST`, slow RAM `$C00000` (gana a MEMF_CHIP), Zorro II, otros.
8. `model_has_rtc`: A500/A1000 no; A500+/A1200/CD32 si.

Cubre por logica las ramas AGA y CD32, que el runner (solo A500) no puede emular.

Objetivo y fuentes del sondeo: `docs/engine/architecture/HARDWARE_INVENTORY.md`.

## Salida de referencia

```
OK: eng::hw inventario (consultas y heuristicas) validado.
```

## Ejecutar

```bash
bash tools/run-host-tests.sh tests/host/platform/amiga/235_hw_info
```
