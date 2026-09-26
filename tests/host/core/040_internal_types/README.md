# Test HOST-040: sistema de tipos internos (vistas con tag + unidades fuertes)

Respalda `eng/core/types/typed.hpp`, el fundamento de `INTERNAL_TYPE_SYSTEM.md`: la vista contigua
con *tag* de dominio `TaggedSpan<T, Tag>` y sus cuatro alias (`Bytes<Tag>`/`ByteView<Tag>`/`Words<Tag>`/
`WordView<Tag>`), y la dirección DMA-visible `Address<MemoryKind::Chip>`.

Se comprueba (vía conceptos, `static_assert`): que un dominio **no** se convierte en otro (audio ≠
patrón, words ≠ bytes, `Span` crudo ≠ vista tipada), que la reinterpretación byte↔word es explícita,
que la vista no añade tamaño sobre `Span`, y el acceso/escritura de las vistas.

```bash
bash tools/run-host-tests.sh tests/host/core/040_internal_types
```

Contexto: `docs/engine/architecture/INTERNAL_TYPE_SYSTEM.md`.
