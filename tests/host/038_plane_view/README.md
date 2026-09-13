# Test HOST-038: PlaneView (soft DPF, doble buffer de planos)

Respalda `eng::field::PlaneView` (`engine/include/eng/field/plane_view.hpp`): la pieza del soft DPF
(RoboCod) extraída del playfield de scroll — una vista de los planos de otro bitmap con doble
buffer opcional.

Se comprueban (con punteros crudos, sin `MemorySystem`): sin doble buffer display y escritura
coinciden; con doble buffer `display_base`/`write_base` seleccionan delantero/trasero; `flip()`
conmuta; y `bind_raw` sin bloque extra queda en modo simple.

```bash
bash tools/run-host-tests.sh tests/host/038_plane_view
```

Contexto: `docs/engine/architecture/PLAYFIELD_SCROLL_ARCHITECTURE.md` §3.2,
`docs/reference/amiga/techniques/robocod-layered-scroll.md`.
