# 132 — scroll de tiras con geometría **runtime**

Tutorial (paso 3 de §7 de `ROADMAP_GAME_API`): el motor de tiras consume una **geometría cargada en
runtime** en vez del NTTP `StripScrollGeometry`. Es el caso del **editor** o de un nivel cuya
geometría no se conoce al compilar.

```cpp
using Layer = eng::playfield::StripScrollLayer<eng::playfield::RuntimeScrollGeometry,
                                               eng::playfield::TilemapView, Backend>;
// init(App&): geometría "cargada" (mismos valores/invariantes que el NTTP, calculados aquí):
auto geo = eng::playfield::runtime_scroll_geometry(320, 256, 3, 16, 16, 2, 1, false, 0, 256+192, 40);
m_layer.set_geometry(*geo);   // antes de set_plan/set_tilemap (derivan tamaños)
m_layer.set_plan(plan);       // el resto igual que la 204
app.add_scroll_layer(m_layer); // el App la arranca y conduce
```

`RuntimeScrollGeometry` tiene los **mismos campos** que `StripScrollGeometry` (que es NTTP); el
motor los lee de la instancia (con la geometría NTTP, los miembros `static constexpr` se pliegan igual
que antes, así que el camino rápido no cambia). Verificado: **HOST-244** (planner + copper + blit con
`RuntimeScrollGeometry` ≡ NTTP) y esta demo (mismo pueblito que la
[204](../204_app_strip_scroll/README.md), con la geometría calculada en runtime).

## Build / run

```bash
bash ./tools/build/build-demo.sh demos/techniques/amiga/playfield/132_runtime_geometry --release
bash ./tools/run/run-demo.sh demos/techniques/amiga/playfield/132_runtime_geometry --warp
```
