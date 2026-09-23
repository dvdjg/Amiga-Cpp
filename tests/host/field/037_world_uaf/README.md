# Test HOST-037: UAF-R → WorldView (chunk WorldMap)

Respalda la cadena del formato de mundo dentro del contenedor canónico: se ensambla un blob
**UAF-R** con un chunk `WorldMap` (con el `BlobWriter` del runtime), se valida con `Blob` y se lee
con `eng::assets::WorldView`. Es decir, `world.bin` → `world.uafr` → runtime.

Se comprueban: `add_chunk`/`finish`, `Blob::bind`/`find`, y la lectura del mundo desde el chunk
(capa, celdas y borde). El contenedor lo produce en el host
`tools/assets/uaf-pack.*` con `--world world.bin`.

```bash
bash tools/run-host-tests.sh tests/host/field/037_world_uaf
```

Contexto: `docs/engine/architecture/WORLD_FORMAT.md`, `docs/tools/UAF_PACK.md`.
