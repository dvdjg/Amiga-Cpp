# DPF mixto: corkscrew/split + linear/mirror (Y independiente por campo)

## El problema

En DPF con dos `XLimitedPlayfield` corkscrew, el compositor usa **un único split
de Copper** vertical: cuando un campo envuelve su anillo (`display_height`) dentro
de la ventana, ambos campos deben compartir `mapposy` (mismo `display_offset` y
`split_line`). Por eso en el DPF "normal" **la Y es común** a los dos playfields
(aunque el X es libre, con su nibble de `BPLCON1` y sus `BPLxPT`).

## La solución: un campo lineal (mirror), estilo Yunlimited

El `XLimitedPlayfield` ya tiene `linear_display` (espejo del bucle): duplica el
buffer vertical (`bitmap_height += display_height`) y el display lee **contiguo**
desde `display_offset`, sin split. Con el espejo, un campo puede estar en
cualquier `mapposy` de forma **independiente** (no hay raster de re-apuntado).
Coste: **2× el buffer vertical y 2× los blits** de ese playfield (cada tile se
dibuja en el bucle y en su espejo).

## Combinaciones válidas (tras `dual_linear_field`)

| Modo | Campo 0 (PF1) | Campo 1 (PF2) | Y | Memoria/blits |
|---|---|---|---|---|
| Corkscrew dual (por defecto) | split | split (línea común) | compartida | anillo ×2 |
| Lineal dual | mirror | mirror | independiente en ambos | (anillo+espejo) ×2 |
| **Mixto** | linear/mirror | corkscrew/split | **FG libre, BG con su recorrido** | espejo en 1, anillo en el otro |
| **Mixto (inverso)** | corkscrew/split | linear/mirror | BG libre, FG con su recorrido | espejo en 1, anillo en el otro |

Cambios de engine:
- `XlimitedSceneConfig.dual_linear_field` (0 = aplicar `linear_display` a ambos;
  1 = lineal solo field0/PF1; 2 = lineal solo field1/PF2).
- `XlimitedDualComposer`: `valid()` admite que cada campo tenga split o no de
  forma independiente (solo exige `split_line` igual si AMBOS tienen split
  activo), y `emit_full` re-apunta en el raster del split **solo** el campo que
  envuelve (el lineal nunca se re-apunta).

## Todas las posibilidades (API del engine)

El engine expone esto de forma **paramétrica** (sin macros); cada juego elige en
`XlimitedSceneConfig`:

```
linear_display        // bool: mirror en AMBOS playfields
dual_linear_field     // 0 = según linear_display · 1 = mirror solo field0 · 2 = solo field1
scroll_y              // activar/desactivar el bucle vertical (corkscrew)
max_step / set_scroll_step  // velocidad por campo
map, map2             // tilemaps independientes (wrap toroidal soportado)
blocks_prebuilt[1|2]  // bancos reales por campo (3..6 planos)
```

Roles de campo (importante): field0 = `map` = PF1 (planos HW 1,3,5); field1 =
`map2` = PF2 (planos HW 2,4,6). En `XlimitedScene` los getters `bg()`/`fg()`
siguen el índice, no el rol visual: `bg()`=field0, `fg()`=field1. En un juego
recomienda fijar el rol con nombres propios (p. ej. una capa «fondo» y otra
«plataformas») y referirse a `m_field[0]/m_field[1]` por su mapa, no por el nombre.

| # | Config | field0 | field1 | Y | Coste mem./blits |
|---|---|---|---|---|---|
| 1 | corkscrew dual | split | split (misma línea) | compartida | anillo ×2 |
| 2 | lineal dual | mirror | mirror | independiente en ambos | (anillo+mirror) ×2 |
| 3 | mixto A | **mirror** | split | **field0 libre**, field1 con su recorrido | mirror en field0 |
| 4 | mixto B | split | **mirror** | field0 con su recorrido, **field1 libre** | mirror en field1 |

Regla de hardware: en OCS solo hay **un** split de Copper por frame → dos campos
corkscrew no pueden envolver en filas distintas; por eso la Y independiente exige
que al menos un campo sea lineal/mirror (o estático/`CanvasPlayfield`).

Coste del mirror: duplica `display_height` filas verticales del bitmap de ESE
campo (p. ej. viewport 208 → +240 filas planelínea) y cada tile se dibuja dos
veces (bucle + espejo). El campo corkscrew/split mantiene el anillo compacto.

Uso típico por tipo de juego:
- Plataformas con BG = fondo simple y FG = mapa de plataformas 8-way → FG suele
  ser el lineal/mirror (Y libre) y BG el corkscrew (o un lienzo fijo).
- Parallax de varias capas → cada capa con Y propia = modos 2/3/4 según cuántas
  capas y memoria disponible.
- Capa de personajes/bobs (estilo Megatyphoon) → capa lineal con un optimizador
  que escriba solo donde el bob no solapa a otros (futuro).

La demo 202 expone el modo 3 por defecto y la variante 1 con `kShareY` (constante
paramétrica).

## Reparto recomendado

- **BG = corkscrew/split**: el tilemap grande (el nivel). El anillo ahorra Chip
  RAM (no duplicas el mundo) y su split es justo lo que necesita para envolver un
  mundo alto.
- **FG = linear/mirror**: capas con Y propia: parallax, personajes/objetos que no
  deben quedar casados al scroll vertical del nivel, o una capa más alta que el
  límite del comparador de 8 bits.

Otras posibilidades (documentadas, no implementadas en la demo):
- Varias capas de parallax (el BG como mero fondo y el FG dibujando el mapa de
  plataformas y el resto, estilo Jim Powers pero 8-way).
- Capa de bobs tipo Megatyphoon en el FG lineal: el espejo duplica, pero un
  optimizador que **evite releer lo que hay en pantalla** y escriba solo donde el
  bob no solape a otros (sprite/colisión) reduciría el coste real por frame.

## Verificación (demo 202)

La demo 202 trae **por defecto** el modo de independencia (FG field0/PF1 lineal,
BG field1/PF2 corkscrew/split) para que se vea Y desacoplada:

```
bash ./tools/run/run-demo.sh demos/202_xlimited_dpf --warp --settle-ms 50000
```

El `detail` del run-status publica `phase<<24 | maxΔY<<12 | bgY`, donde `maxΔY` es
el máximo `|fgY-bgY|` observado durante toda la ejecución. En fase Lissajous se
observan, por ejemplo, instantes con `bgY≈272` y `fgY≈115` (Δ grande), lo que
demuestra que **las dos Y son independientes**. En la variante corkscrew dual
compartida `maxΔY≈0`.

Variante corkscrew dual clásico (Y compartida): en `demos/202_xlimited_dpf/src/main.cpp`
pon `static constexpr bool kShareY = true;` (constante paramétrica con `if constexpr`,
sin macros) y recompila. El modo se elige así en el código; el ENGINE no usa macros:
la vía es siempre `XlimitedSceneConfig.dual_linear_field` (0 = ambos corkscrew /
split compartido, 1 = lineal field0, 2 = lineal field1).
