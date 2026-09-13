# RoboCod — scroll por capas con fondo más lento (parallax) en Amiga OCS

Técnica de **dos (o más) capas de scroll independientes**: un **primer plano de juego**
(p. ej. plataformas) que ocupa poco de la pantalla y **deja ver por transparencia** un
**fondo más grande que scrollea más lento** (*parallax*). Referencia de estilo: *James Pond 2:
Codename RoboCod*. En el original el fondo era un plano de 1 bit con áreas amplias y se
enriquecía con **cambios de color por raster (Copper)** para "aparentar" más de un bitplane.

## 1. Idea y por qué funciona

```text
  ┌───────────────────────────────────────────────┐
  │ FG (poca cobertura, ~15%): plataformas/bloques │  scroll RÁPIDO (velocidad de juego)
  │   transparente el 85% restante                 │
  ├───────────────────────────────────────────────┤
  │ BG (áreas amplias): patrón/motivo de fondo      │  scroll LENTO (p. ej. 1/2, 1/4…)
  └───────────────────────────────────────────────┘
```

- El **movimiento relativo** entre capas crea la sensación de profundidad (el fondo "está
  más lejos"). No hace falta que el fondo sea más pequeño: basta que **scrollee más lento**.
- El FG **ocupa poco** y es **transparente** donde no hay nada; así el fondo domina la imagen.
- Los **colores deben contrastar** entre capas (p. ej. FG cálido naranja/amarillo sobre BG
  frío azul) para distinguirlas de un vistazo.

## 2. Implementación en el engine (canónica): DPF de 2 capas

Se implementa con **dual playfield** (dos `XLimitedPlayfield`, cada uno con su bitmap) en
`XlimitedScene`:

- `scene_cfg.dpf.enabled = true` y `planes = 3` (DPF 3+3 = 6 planos HW; 8 colores por capa,
  **16 en total**). Roles: `bg()` = `field[0]` = **PF1** (delante), `fg()` = `field[1]` = **PF2**
  (detrás). Pon el **primer plano de juego en PF1** (delante).
- **Paleta de 16 registros**: PF1 usa 0..7, PF2 usa 8..15.
- **Parallax**: cada campo tiene **su propia cámara** (`set_camera`) y se avanza con
  `update_scroll` a distinta velocidad:
  ```cpp
  scene.bg().update_scroll(plan, dx, dy);          // FG (PF1): velocidad de juego
  scene.fg().update_scroll(plan, dx / 2, dy / 2);  // BG (PF2): mitad (parallax)
  ```
- **Dos bitmaps separados** ⇒ el blit del FG **no borra** el BG (ver §3).
- Transparencia: el color 0 de **cada** playfield es transparente; el FG deja ver el BG.

## 3. Bloqueo del "5.º plano" en un solo playfield (por qué NO)

La tentación es un **único playfield de 5 planos** (4 = FG, 1 = patrón de fondo). **No
funciona bien**: el blit del tilemap FG es un **TileBlockCopy interleaved** que cubre los 5
planos y escribe 0 en el plano del fondo, **borrando el patrón en cada fila/columna que entra
al scrollear** (el fondo solo sobrevive donde el FG no ha vuelto a pintar). El Blitter
**no puede saltar un plano intermedio** en un blit interleaved (el layout es
`[scanline][plano][bytes]`); harían falta **blits por plano** (4 blits en vez de 1), más lentos
y que el engine no emitía. Conclusión: usa **DPF** (bitmaps separados) para capas de scroll.

## 4. Motivo de fondo con **tiles** (no una imagen fija)

El fondo es un **tilemap** normal (`map2` + `bg_row_fn`). Un patrón de **bandas diagonales
sin costura** se logra con un tile cuyo contenido depende de `(x_local + row)`:

```cpp
eng::u16 bg_row(glyph, variant, row, plane) {
  eng::u16 mask = 0;
  for (u8 x = 0; x < 16; ++x)          // tile 16x16
    if (((x + row) & 15u) < 8u) mask |= 0x8000u >> x;   // franja diagonal, periodo 16
  const u8 c = 6;                       // color del patrón (índice 1..7 de PF2)
  return (c & (1u << plane)) ? mask : 0;
}
```

Como el patrón depende de `(x_local + row)` y se repite cada 16 px, **no hay costura** entre
tiles contiguos y el motivo "tilea" — lo que demuestra que el fondo **usa tiles**. Un motivo
extra (rombos, cruces) es cuestión de añadir más `glyphs` al tileset.

## 5. Raster colors (Copper) para "aparentar" más de 1 bitplane

El plano de fondo solo tiene 2 estados (patrón on/off). Para dar riqueza, se cambia el **color
del patrón** (`COLORxx` del registro del BG) **por línea de raster** con el Copper: cada zona
de líneas usa un **color pastel distinto**, siempre contrastando con el color de "off"
(normalmente negro/transparente). Así el mismo bit de fondo aparenta varios tonos.

- El engine expone el `copper::Scheduler` para emitir `MOVE COLORxx` precedidos de `WAIT` en
  la línea de la zona (`wait_line` + `move`).
- Regla: los colores de zona deben **contrastar** entre sí y con el "off" (si dos zonas
  contiguas usan colores parecidos, el degradado no se percibe).

## 6. Parámetros y límites (A500/OCS)

- DPF 3+3: **6 planos HW**, 8 colores/capa, **más DMA y Chip RAM** que single.
- **Nº de planos del fondo**: aquí el fondo es **PF2 con 3 planos (8 colores)**, NO 1 bit. El patrón usa 2 (bandas + puntos) de esos 8; el resto queda libre. El RoboCod original usaba **1 bit** (2 colores); reproducirlo literal exige DPF **asimétrico** (p. ej. 3+1 o 4+1), que el compositor dual actual **no** soporta: usa un `planes_per_field` **simétrico** (3+3). Añadir 3+1/4+1 es una extensión pendiente.
- Paleta DPF: PF1 = registros 0..7, PF2 = 8..15. `BPLCON2` decide prioridad (`PF2PRI`).
- Scroll por capa: cada `XLimitedPlayfield` conserva su `planeaddx`/`bplcon1`; el compositor
  dual programa `BPLCON1` (nibble bajo = PF1, alto = PF2) y los `BPLxPT` de ambos.
- Split vertical (corkscrew) compartido si ambos campos envuelven en Y; en DPF MIXTO un campo
  puede ser lineal/mirror (sin split, Y libre).

## 7. Demo de referencia

`demos/amiga/112_xlimited_robocod`: DPF 3+3, FG plataformas naranjas (~15%) sobre BG de bandas
diagonales azules a mitad de velocidad, con rebote diagonal (X: 1024 px, Y: 512 px → se ve el
área mayor). Verificación visual: `analyze-sequence.sh` + `ollama-desc.mjs` (ver
`docs/guides/methodology/DEMO_VISUAL_DEBUG.md`).
