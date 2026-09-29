# HOST-315 — Variantes de fuente (cursiva + micro)

## Qué cubre

`engine/include/eng/graphics/font_italic.hpp`: variantes **derivadas** de las fuentes existentes,
sin duplicar tablas:
- **Cursiva** (*italic*) por *shear* horizontal progresivo por fila: `italic_shift`,
  `font8_italic_row`, `font5x7_italic_row`, `font8_row_italic`, `font5x7_row_italic`. Se obtiene la
  versión cursiva de `Font8` (8×8) y de `Font5x7` (5×7, HUD).
- **Micro-fuente** `Font3x5` derivada de `Font5x7` (3 de sus 5 columnas, 5 de sus 7 filas) para
  micro-etiquetas, con su variante cursiva (`row_italic`).

Completa la colección de glifos del engine: `Font8`, `Font5x7`, `Font3x5`, cada una con cursiva.

## Invariantes

1. `italic_shift(r, rows, max_slant)`: 0 en la fila superior, `max_slant` en la inferior, no decrece.
2. La cursiva **nunca añade** bits a un glifo (puede recortar en el borde; el avance debe crecer
   `slant` px si se quiere evitar el recorte).
3. `Font3x5` usa ≤ 3 bits de ancho y lee de `Font5x7` (mismos code points soportados).

## Ejecución

```bash
bash tools/run-host-tests.sh tests/host/ui/315_font_variants
```

## Relación

- Diseño: `docs/engine/architecture/GUI_LIBRARY.md` §6; plan: `docs/guides/roadmap/ROADMAP_GUI.md`.
- Fuentes base: `eng/graphics/font8.hpp`, `eng/graphics/font5x7.hpp`.
