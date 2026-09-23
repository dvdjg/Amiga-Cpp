# HOST-018 — Reloj de tiempo real (TOD de la CIA-A)

Valida `engine/include/eng/core/rtc.hpp`: la conversión pura del contador **TOD** de la
CIA-A (24 bits) a hora del día.

## Qué cubre

- Contador 0 → `00:00:00`, subsegundo (`ticks`).
- `50 Hz` (PAL) y `60 Hz` (NTSC): 50/60 ticks = 1 s.
- `1:01:01`, `23:59:59` y **envuelta a las 24 h**.
- Propagación de `hz` y `total_seconds`.

La **lectura** del registro la hace el backend (`MinimalBackend::cia_tod_ticks`, con el
orden de latch `TODHI → TODMID → TODLO`). Referencia:
`amiga-bootcamp/01_hardware/common/cia_chips.md` ("Time-of-Day").

## Ejecución

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/018_rtc
```
