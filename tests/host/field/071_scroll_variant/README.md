# HOST-071: selección de variantes de scroll

`eng/field/scroll_variant.hpp`: traduce los nombres de la referencia **ScrollingTricks**
(`XLimited`, `XUnlimited`, `YUnlimited(2)`, `XYLimited`, `XYUnlimited(2)`) a los ejes/wrap/fetch de
`XlimitedConfigT`, más los presets **tall-Y** (XLimited con Y > viewport) y **wide-X** (YLimited con
banda de 384 px, fetch 1x4x). Ver [`SCROLL_VARIANTS.md`](../../../../docs/engine/architecture/SCROLL_VARIANTS.md).

## Qué comprueba

- `XLimited`: X anillo, Y off, sin wrap; `XUnlimited`: X toroidal (`wrap_x = width`).
- `YUnlimited*`: Y anillo, toroidal (`wrap_y = height`).
- `XYLimited`: ambos anillo sin wrap; `XYUnlimited*`: ambos toroidales.
- `_64` (`wide_fetch`): `fetch_mode = 3` (1x4x) y `bitmap_width = viewport_w + 64`.
- `xlimited_tall_y_display`: `display_height = viewport_h + 2*tile_h`.
- `apply_ylimited_wide_x`: YUnlimited con fetch ancho y X acotado.
- `variant_video_split`: marca las variantes `2` (video-splitting).
