# Test HOST-039: SoftDpfComposition (soft DPF extraído del playfield)

Respalda `eng::field::SoftDpfComposition` (`engine/include/eng/field/soft_dpf.hpp`): la composición
soft DPF (RoboCod) extraída de `XLimitedPlayfield` — vista del plano de fondo con doble buffer +
geometría + construcción de los blits de copia.

Se comprueban (con punteros crudos): activación según `parallax_plane`/`planes`, `display_base`/
`write_base`, el destino del blit en el buffer trasero (y en el principal tras `flip`), el origen
con el shift del barrel shifter, los módulos, `make_copy_job` (fila completa) y el caso inactivo.

```bash
bash tools/run-host-tests.sh tests/host/field/039_soft_dpf
```

Contexto: `docs/engine/architecture/PLAYFIELD_SCROLL_ARCHITECTURE.md` §3.2,
`docs/reference/amiga/techniques/robocod-layered-scroll.md`.
