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

El `detail` del run-status publica `phase<<24 | fgY<<12 | bgY`. En fase Lissajous
se observa, por ejemplo, `bgY≈272` mientras `fgY≈115`, lo que demuestra que **las
dos Y son independientes**.

Variante corkscrew dual clásico (Y compartida):

```
EXTRA_DEFINES="-DK_DUAL_SHARE_Y=1" bash ./tools/build/build-demo.sh demos/202_xlimited_dpf --debug --clean
bash ./tools/run/run-demo.sh demos/202_xlimited_dpf --config A500_k_ual_share_y1_debug --warp --settle-ms 45000
```
