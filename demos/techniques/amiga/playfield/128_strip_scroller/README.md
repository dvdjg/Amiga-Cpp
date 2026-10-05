# 128 — scroller por tiras (Copper ring + incoming strip)

Referencia del **camino rápido de scroll**: **50 fps (1 campo)** en A500 single-playfield (3 planos,
8 colores), CPU baja en el hotpath. Usa el camino implementado en `eng/field/strip_scroller.hpp` +
`strip_composer.hpp` (ver `docs/engine/architecture/SCROLL_VARIANTS.md §4.1`).

```bash
bash ./tools/build/build-demo.sh demos/techniques/amiga/playfield/128_strip_scroller --release
bash ./tools/run/run-demo.sh demos/techniques/amiga/playfield/128_strip_scroller
```

## Qué hace

- Anillo del **mapa completo + una pantalla de solape** (60 words = 40 columnas de mapa + 20
  visibles); viewport 320×256; 3 planos interleaved (atlas “Beginning Fields”).
- **Objetos singulares** de colores distintos (bloques 32×32 cada 6 columnas) sobre fondo azul tenue:
  su desplazamiento horizontal es **inequívoco** (se ve a ojo y lo verifica el modelo de visión).
- Por frame (`kStepX = 2` px): el **CPU+Blitter** lo lleva `playfield::StripScrollController` (planifica
  la tira → `compose_column` desde los tiles **separados**, tile a tile → `blitter_strip_column` a la
  guarda); el **Copper** lo lleva `StripComposer` (`strip_copper_values` + `patch`: `BPLCON1` fine +
  `BPLxPT` por plano) sin re-emitir la lista. La demo ya **no** reimplementa esa lógica.

## Puntos clave (lecciones)

- **`BPL1MOD` depende del ancho FETCHEADO** por el display (~21 words), no del ancho del anillo:
  `BPL1MOD = planes*ring_w_bytes − fetch_words*2`. Con la fórmula "completa" el display lee mal el
  interleave (aparecen rayas en vez de tiles). Ver `StripScrollGeometry::bpl_mod`.
- **`BPLCON1` (fine) usa la convención canónica** `fine_delay(x) = (16 − (x&15)) & 15` y el coarse
  `(x−1) & ~15` (`playfield_scroll.hpp`). Con el fine "directo" (`x & 15`) el fondo **salta 16 px** al
  cruzar palabra (trompicones); con la canónica `visible_left = x` (movimiento suave).
- **`BPLCON1` se escribe con el retardo DUPLICADO en los dos nibbles** (`fine | (fine << 4)`), no
  solo en el bajo. En un playfield single el nibble ALTO sigue afectando a los planos pares
  (BPL2/4/6): con solo el nibble bajo, **el plano central no recibe el fino y se desplaza en saltos
  de 16 px** mientras los otros van suaves. Mismo convenio que `amiga_display_mapper.hpp` (demo 120);
  verificado plano a plano (color→bits) sobre secuencias.
- Los tiles del mapa **no se asumen contiguos**: `compose_column` los copia tile a tile.
- `BLTSIZE` tiene H de 10 bits (máx 1024): la columna se parte en `column_blits`.
- **Dimensionado del anillo (clave)**: el puntero recorre `span = ring − visible` words antes de
  envolver; para que el contenido **no se descuadre al envolver**, `span` debe ser **múltiplo del
  período del mapa**. Con el mapa toroidal de 40 columnas: `span = 40` → `ring = 60`. Un anillo
  “de 2 pantallas” (43 words, span 23) deja los slots fuera de la zona que rueda con contenido
  viejo y la imagen **salta ~1 pantalla cada envolver** (y la palabra extra de fetch nunca se
  pre-pinta, así que un plano del borde derecho se mueve “a trompicones”, sin fine). HOST-244.

## Límites

- Single-playfield. El **DPF 3+3** satura el bus (≤25 fps); ver `BUS_BUDGET.md`.
- El mapa es un patrón repetido; un mapa largo necesita stream de tiles por chunk.

## Validación visual (regla de oro: Ollama)

Medido: **49,92 fps** (1,002 campos/frame) en release. Validado con el modelo de visión local
(`node tools/analyze/ollama-desc.mjs ... `, sobre una **secuencia**, no una captura): movimiento
horizontal **suave, uniforme y continuo, sin saltos de 16 px ni huecos**.

Chequeo automático de movimiento (determinista):

```bash
bash tools/vision-review/check-motion.sh demos/techniques/amiga/playfield/128_strip_scroller
```

Captura una secuencia y `motion-check.py` comprueba: no congelado, dirección correcta y sin saltos
enormes (así se caza el fallo de `BPLCON1` invertido).
