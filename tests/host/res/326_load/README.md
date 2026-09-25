# HOST-326 — carga tipada de assets (`eng::res::load<Tag>`)

Respalda `engine/include/eng/res/load.hpp`: la carga síncrona de bytes a un `Block<Tag>`
que sustituye el patrón `allocate_block<Tag>(bytes + headroom, align)` + `memcpy`. Cubre:

- el **medio/alineación por dominio** (`DomainAsset<Tag>`): planos/BOB/copper a Chip
  alineados a 16, música/samples a Chip alineados a 4;
- la **copia** correcta de los bytes y la reserva con margen;
- el **rechazo sin excepciones**: fuente vacía u overflow devuelven bloque inválido (la
  arena registra el overflow);
- `MemoryKind::Fast` se sirve de la arena `slow` (el engine no separa una arena Fast propia).

Es la mitad **síncrona** de `PUBLIC_GAME_API.md` §2.1.4 (fuente embebida/ya en RAM); la
variante de fichero asíncrona se apoya en `AssetCache` + `os::file_*`.

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/res/326_load
```

Relacionado: HOST-325 (`Budget`) y la demo 213 (gate on-target de la carga a Chip).
