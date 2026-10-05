# 131 — split-screen por la fachada `App` (`present_layout`)

Tutorial: el juego compone el **layout de bandas** con el vocabulario del planner
(`ScenePlan` + `plan_raster_layout` sobre las vistas de sus campos) y se lo entrega al `App` con
**`app.present_layout(layout)`**. El `App` **materializa la copperlist (de la que es dueño, en Chip)
y hace el `takeover`**: el juego declara y el `App` compone, sin nombrar registros ni manipular un
`copper::Scheduler`.

```cpp
eng::scene::RasterLayout layout {};
eng::scene::plan_raster_layout(m_scene_plan, views, layout);   // una banda por capa
layout[0].palette = kPalette.words();
app.present_layout(layout);                                    // el App toma el display

// BOBs por banda (el App los rutea al BobTarget de SU banda):
app.emit_bobs_banded(m_bobs, bands, targets);
```

Muestra un **split-screen** (banda superior roja, inferior azul), una barra vertical que **cruza la
línea de corte** (dibujo *split-aware* con `for_each_band_part`: el trozo de arriba va a un bitmap y
el de abajo al otro) y un **marcador por banda** (`FastBobLayer::emit_banded`). Es la versión "por la
fachada" de la demo de técnica [129_split_screen_bands](../129_split_screen_bands/README.md): allí el
juego baja al `RasterLayout`/`Scheduler`; aquí lo posee el `App`.

## Build / run

```bash
bash ./tools/build/build-demo.sh demos/techniques/amiga/playfield/131_app_split_screen --release
bash ./tools/run/run-demo.sh demos/techniques/amiga/playfield/131_app_split_screen --warp
```
