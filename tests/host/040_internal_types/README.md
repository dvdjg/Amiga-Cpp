# Test HOST-040: sistema de tipos internos (vistas con tag + unidades fuertes)

Respalda `eng/core/typed.hpp`, el fundamento de `INTERNAL_TYPE_SYSTEM.md`: vistas contiguas con
*tag* de dominio (`Bytes<Tag>`/`ByteView<Tag>`/`Words<Tag>`/`WordView<Tag>`), unidades fuertes
(`PlaneIndex`/`PlaneCount`, `RowBytes`, `PixelWidth`…) y bases con semántica distinta
(`BitmapBase`/`FrontBase`/`ChipAddress`).

Se comprueba (vía conceptos, `static_assert`): que un dominio **no** se convierte en otro (audio ≠
patrón, words ≠ bytes, base ≠ front, `Span` crudo ≠ vista tipada), que la reinterpretación
byte↔word es explícita, que la vista no añade tamaño sobre `Span`, la validación de rango de
`PlaneIndex` y el acceso/escritura de las vistas.

```bash
bash tools/run-host-tests.sh tests/host/040_internal_types
```

Contexto: `docs/engine/architecture/INTERNAL_TYPE_SYSTEM.md`.
