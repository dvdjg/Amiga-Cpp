# Test HOST-041: Block\<Tag\> (reserva de arena tipada)

Respalda `eng::Block<Tag>` (`engine/include/eng/core/typed.hpp`) y su uso desde la arena
(`LinearArena::allocate_block<Tag>()`, `MemoryBlock::block<Tag>()`): la reserva devuelve la vista
del dominio, de modo que el consumidor no necesita casts.

Se comprueban: bloque válido con tamaño correcto, acceso (`operator*`/`operator[]`), escritura sobre
la memoria de la arena, segundo bloque, overflow → inválido, `MemoryBlock::block<Tag>()`, y que
`Block` de dominios distintos no se convierte (ni añade tamaño sobre `Bytes`).

```bash
bash tools/run-host-tests.sh tests/host/041_block
```

Contexto: `docs/engine/architecture/INTERNAL_TYPE_SYSTEM.md` (Fase 7).
