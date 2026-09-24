# Demo 122 — Doble buffer de bitmap + swap (`COP1LC`) con scroll

Tercera demo de la matriz de algoritmos (`docs/guides/roadmap/SCROLL_DEMOS_CLEANUP.md`): el **doble
buffer de bitmap**. Dos bitmaps completos del mundo; el display lee el **delantero** mientras la
aplicación escribe en el **trasero**; cada frame se conmuta (`flip()`) y el compositor instala la
copperlist que apunta al nuevo delantero (**swap de `COP1LC`**).

```
  ┌────────────┐   muestra    ┌────────────┐
  │  buffer A  │ ◄─────────── │  display   │
  │ (delantero)│              └────────────┘
  ├────────────┤
  │  buffer B  │ ◄── se escribe aquí; al terminar, flip()
  │ (trasero)  │
  └────────────┘
```

## Qué demuestra

- **`eng::field::DoubleBufferScrollPlayfield`** (engine): dos `gfx::Bitmap`, cámara X/Y saturada y
  `flip()` que conmuta el delantero. Reutiliza el mapper flat `eng::field::map_flat_scroll`
  (HOST-061): el scroll sigue siendo por punteros, y el swap solo cambia la base del bitmap.
- **Escribir filas visibles sin tearing**: es la diferencia con el bitmap único (120/121), que solo
  puede escribir en la banda de staging. El doble buffer es la base para redibujar en el trasero
  mientras el delantero se muestra.
- **Contraste con el corkscrew/XYLimited**: allí el swap es de *copperlist*; aquí además es de
  *bitmap*, lo que duplica el coste de Chip RAM.

## Geometría y coste

- Mundo `448 × 512`, ventana `320 × 256`, 3 planos (8 colores). Dos bitmaps de `56 × 512 × 3 =
  86 KB` → **172 KB** de Chip RAM + banco del atlas (111 KB) + copper (~3 KB).

## Build / run / verify

```bash
bash ./tools/build/build-demo.sh demos/techniques/amiga/playfield/122_doublebuffer_scroll --debug --clean
bash ./tools/run/run-demo.sh demos/techniques/amiga/playfield/122_doublebuffer_scroll
bash ./demos/techniques/amiga/playfield/122_doublebuffer_scroll/analyze-sequence.sh [--warp]
```

`analyze-sequence.sh` comprueba secuencia animada y lee `detail` (marcador `0x14`, buffer delantero
en el bit 20, camX en bits 19..12, camY en bits 11..0).

## Nota de diseño

En esta demo ambos buffers contienen el mismo mundo **ya dibujado** (el patrón es estático y solo
mueven los punteros), así que el swap es deliberadamente transparente: demuestra el **mecanismo**
(par de bitmaps + conmutación + `COP1LC`) que necesita una aplicación que redibuja el frame en el
trasero. La variante útil —dibujar el frame en el trasero cada frame y conmutar— es el siguiente
paso natural (capa de bobs/personajes sobre este scroll).

## Estado

- `DoubleBufferScrollPlayfield` verificada por demo (regla de cierre de `docs/testing/README.md`).
- Pendiente: capa que redibuje el trasero por frame, y el mapper de anillo/split del corkscrew.
