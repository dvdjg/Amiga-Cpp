# HOST-341 — prueba de decisión §7.6 (adaptador de consumidor sobre `api.hpp`)

Demuestra la decisión del roadmap (§7.6): las interfaces `I*` de un **consumidor externo**
**viven fuera del engine** y se implementan con **solo** `<eng/api/api.hpp>` + los helpers
generales, sin tocar `field`/`BobTarget`/planos ni registros. El test define interfaces externas
(`IChipMem`, `ICopper`) y **adaptadores finos** sobre el engine:

- `IChipMem` → `eng::res::ChipPool` (alloc/free/free_bytes);
- `ICopper` → `eng::Copper` (fachada de alto nivel sobre el `Scheduler`).

Si esto compila y funciona, la frontera es correcta y **el engine no se ve afectado** por la
interfaz del consumidor.

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/core/341_adapter
```

Ver `docs/engine/architecture/ROADMAP_API_COHERENCE.md` §7.
