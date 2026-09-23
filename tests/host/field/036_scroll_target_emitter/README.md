# Test HOST-036: contrato del scroll separado (ScrollTarget + ScrollEmitter)

Respalda la separación del sink del `ScrollEngine` (`engine/include/eng/field/scroll_engine.hpp`)
en dos mitades, según `PLAYFIELD_SCROLL_ARCHITECTURE.md` §3.1:

- `ScrollTarget`: geometría del anillo/layout (sin dibujar).
- `ScrollEmitter`: dibujo de la banda + costura (`save_word`/`restore_saveword`).
- `ScrollSink = ScrollTarget && ScrollEmitter`.

Se comprueba que una mitad no implica la otra, que el sink completo exige ambas, y (con
`static_assert`) que `XLimitedPlayfield` cumple las tres.

```bash
bash tools/run-host-tests.sh tests/host/field/036_scroll_target_emitter
```

Contexto: `docs/engine/architecture/PLAYFIELD_SCROLL_ARCHITECTURE.md` §3.1.
