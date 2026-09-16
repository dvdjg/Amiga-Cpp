# Demo 121 — Espejo vertical (scroll de 256 px sin split de Copper)

Segunda demo de la matriz de algoritmos (`docs/guides/roadmap/SCROLL_DEMOS_CLEANUP.md`): el
**espejo vertical**. El bitmap contiene el bucle de display **duplicado** (filas `[0,288)` y su copia
en `[288,576)`); el display lee de forma **contigua** desde la cámara y **no hace falta split**.

```
  bitmap 448 x 576                         ventana 320 x 256
  ┌──────────────────────────┐
  │  bucle    filas 0..287    │ ─┐
  ├──────────────────────────┤  │ el display lee contiguo desde cam_y
  │  espejo   filas 288..575  │ ─┘ (si cam_y>32, sigue en el espejo) → sin split
  └──────────────────────────┘
```

## Qué demuestra

- **`eng::field::MirrorScrollPlayfield`** (engine): bitmap con el bucle duplicado; cámara X saturada
  (revee) y cámara Y **envolvente** dentro del bucle. Reutiliza el mapper flat
  `eng::field::map_flat_scroll` (HOST-056): el espejo permite leer contiguo desde `cam_y` sin
  envolver el puntero, así que el mapeo horizontal/vertical es el mismo que el virtual playfield.
- **Scroll vertical de 256 px sin split**, la alternativa recomendada en
  `docs/guides/roadmap/CONSULTA-SPLIT-208.md` (el comparador de 8 bits del Copper no permite un
  split móvil fiable por encima de ~214 px).
- **Contraste con el corkscrew/XYLimited (107)**: el anillo compacto + split es más barato en Chip
  RAM pero limitado a campos ≤214 px; el espejo paga 2× el alto del bucle y a cambio da scroll
  completo sin artefacto.

## Geometría y coste

- Mundo `448 × 576` (bucle 288 + espejo 288), ventana `320 × 256`, 3 planos (8 colores).
- Bitmap: `56 × 576 × 3 = 94,5 KB` en Chip RAM + banco del atlas (111 KB) + copper (~3 KB).

## Build / run / verify

```bash
bash ./tools/build/build-demo.sh demos/amiga/121_mirror_scroll --debug --clean
bash ./tools/run/run-demo.sh demos/amiga/121_mirror_scroll
bash ./demos/amiga/121_mirror_scroll/analyze-sequence.sh [--warp]
```

`analyze-sequence.sh` comprueba secuencia animada y lee `detail` (marcador `0x13`, camX en bits
19..12, camY en bits 11..0).

## Estado

- `MirrorScrollPlayfield` verificada por demo (regla de cierre de `AGENTS.md`).
- Pendiente: capa de doble buffer + swap `COP1LC` (la otra demo de la matriz), y generalizar el
  mapper para el anillo/split del corkscrew.
