# HOST-446 — Seguridad de copperlist (slots tipados, validadores, presupuesto)

Cubre la capa que impide que una copperlist mal formada o una combinación de hardware
absurda llegue a pantalla (contexto: `docs/engine/architecture/COHERENCIA_DE_SETUP.md`).

## Qué ejercita

1. **Slots tipados** (`eng/copper/copper.hpp`): `move_at` devuelve `MoveSlot` y los `wait_*`
   devuelven `WaitSlot`; `patch_data`/`patch_move_reg`/`patch_wait` (miembros y helpers
   libres sobre `Span<u16>`) escriben la palabra correcta. Pasar el índice de un WAIT a un
   parche de MOVE no compila (el tipo es la garantía). `patch_wait` con `vpos > 255` marca
   el builder no válido (la estructura necesitaría el par de overflow).
2. **`eng::setup::check_copper_list`**: acepta la lista real de un `Scheduler` y rechaza sin
   terminador `END`, MOVEs a registros de solo lectura/huecos/chipset ajeno
   (`copper_register_access`), instrucciones tras el `END`, `0xffff` con máscara distinta de
   `0xfffe` (espera infinita), MOVEs con bits no canónicos y WAITs que retroceden (con el par
   de overflow PAL como reinicio). Fuente de las reglas: WinUAE-DBG `custom.cpp` (decodificación
   y `COP_waitforever`) e `identify.cpp` (mapa de registros), citadas en la cabecera.
3. **`eng::setup::check_copper_column`**: valida el `ColumnPatch` de un relleno con paso
   (blitter strided / copper-chunky) contra las instrucciones emitidas: stride par ≥ 2, filas
   dentro de la lista y cada destino sobre el DATO de un MOVE.
4. **Presupuesto en compilación** (`compile_intents`): `static_assert` sobre
   `visible_over_budget_lines`/`heaviest_line_moves` con una escena ligera y una escena
   «malvada» (40 MOVEs en una línea visible) que el presupuesto HBlank de la `Timeline`
   detectaría. El compilador emite también el terminador `END`.
5. **Mapa de registros por chipset** y `ScheduleReport::publishable()` (criterio que
   `Plan::end_frame` endurece con `ENG_COPPER_STRICT`).

## Ejecución

```bash
bash tools/run-host-tests.sh tests/host/graphics/446_copper_list_safety
```
