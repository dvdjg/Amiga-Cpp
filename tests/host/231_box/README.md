# HOST-231: `eng::Box` y adaptadores de rectángulos

Test host del **tipo único de rectángulo** `eng::Box` (`engine/include/eng/core/box.hpp`) y de sus
conversiones a/desde los tipos con semántica distinta que ya existían en el engine.

## Qué comprueba

1. Helpers de `Box`: `empty`, `right`/`bottom` inclusivos, `contains` inclusivo, `inset`,
   `from_ltrb`, `overlaps`, `intersect`, `merge`, `translate`.
2. **Round-trip** `Box` ↔ `field::SurfaceRect` (`s32` + `w/h`).
3. **Round-trip** `Box` ↔ `field::ClipRect` (bordes inclusivos `x1`/`y1`).
4. **Round-trip** `Box` ↔ `graphics::DirtyRect` (bordes **exclusivos**), y `DirtyRect` inválido
   → `Box` vacío.

Motivo: había cuatro rectángulos con convenciones de borde distintas (`SurfaceRect`, `ClipRect`,
`DirtyRect`, `eng::Rect` en `linalg`), fuente real de errores de 1 px. `Box` es el tipo común; los
adaptadores viven en cada capa, sin duplicar `contains`/`inset`/`intersect`.

## Salida de referencia

```
OK: Box y adaptadores (SurfaceRect/ClipRect/DirtyRect) validados.
```

## Ejecutar

```bash
bash tools/run-host-tests.sh tests/host/231_box
```
