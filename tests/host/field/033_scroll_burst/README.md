# Test HOST-033: avance en fronteras de tile (prefill/latch) y staging en Y

Respalda la parte de comportamiento de los perfiles rápidos:
`eng::field::snap_to_tiles` y `ScrollProfile::y_staging_tiles`
(`engine/include/eng/field/scroll_profile.hpp`).

Se comprueban: redondeo a tiles completos con signo (`snap_to_tiles`), el staging vertical del
corkscrew (2 bloques clásico; `guard_tiles` en los rápidos), que `ScrollFastN` activa
`prefill`/`direction_latched` y que el paso rápido es múltiplo de tile (dirección laceda a
frontera).

```bash
bash tools/run-host-tests.sh tests/host/field/033_scroll_burst
```

Contexto: `docs/engine/architecture/FAST_SCROLL.md`.
