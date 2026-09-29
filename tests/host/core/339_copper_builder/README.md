# HOST-339 — fachada de Copper de alto nivel (`eng::Copper`)

Respalda `engine/include/eng/api/copper.hpp`: construye la copperlist por **intención** sobre un
`copper::Scheduler` sin nombrar `WAIT`/`MOVE` ni registros:

- `wait_line(line)`, `set_color(index, 0x0RGB)`, `set_palette(PaletteWords, first, count)`,
  `set_scroll(bplcon1)`, `words_used()` (presupuesto consumido).

Es el helper general para que un consumidor externo construya su copperlist sin tocar el Copper. El
ciclo de vida (doble buffer, `begin`/`commit`, instalación) lo llevan `Scene`/`Device`
(`scene.begin_build`/`end_build`, `app.present()`, `device.commit_copper`); expuesto en
`Device::copper_builder()`.

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/core/339_copper_builder
```

Ver `ROADMAP_API_COHERENCE.md` §7 (F7.4).
