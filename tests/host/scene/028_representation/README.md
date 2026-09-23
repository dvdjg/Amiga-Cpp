# Test HOST-028: representación de actores (sprite/BOB/CPU/playfield)

Respalda `eng::scene::choose_representation` / `RepresentationAllocator`
(`engine/include/eng/scene/representation.hpp`): la aplicación describe el actor
(plantilla: tamaño, planos, representación preferida, prioridad y si necesita scroll
propio) y el motor decide **cómo materializarlo**, consumiendo presupuesto y
reasignando cuando un recurso se agota.

Se comprueban: elección por tamaño (sprite vs BOB), preferencia de capa con scroll
propio (playfield-as-actor tipo Jim Power), degradación a CPU sin Blitter ni sprites,
canales de sprite consumidos y reasignación a BOB al agotarse, y el consumo del
presupuesto de BOB y de slots de capa.

```bash
bash tools/run-host-tests.sh tests/host/scene/028_representation
```

Contexto: `docs/engine/architecture/SCENE_AND_RESOURCES.md` §2 y §3.
