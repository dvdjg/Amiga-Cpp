# 130 — escalera de motores por la fachada `App` (geometría runtime, §7)

Tutorial: el juego declara **dos configuraciones conocidas** (dos geometrías de capa de tiras) y las
registra en una **`ScrollLadder`**; luego **una geometría cargada en runtime** elige el motor con
`app.pick_scroll_engine(ladder, geometry)`, que lo **arranca y lo conduce**. Es el camino para el caso
"el nivel/editor **no conoce la geometría en compilación**" **sin** refactorizar el motor NTTP: el
juego aporta los motores que conoce (uno por nivel) y el `App` **elige por geometría**.

```cpp
ScrollLadder<AmigaBackend> ladder;
ladder.add(layer_a, runtime_scroll_geometry(320, 256, 3, 16, 16, ...)); // nivel A
ladder.add(layer_b, runtime_scroll_geometry(320, 208, 3, 16, 16, ...)); // nivel B
// ... cargar la geometría del nivel:
app.pick_scroll_engine(ladder, loaded_geometry);   // elige + arranca + conduce
```

Muestra el **pueblito** (atlas "Beginning Fields" por el camino de tiras), igual que la 204 —
con el motor elegido **en runtime** por la escalera.

## Build / run

```bash
bash ./tools/build/build-demo.sh demos/techniques/amiga/playfield/130_app_scroll_ladder --release
bash ./tools/run/run-demo.sh demos/techniques/amiga/playfield/130_app_scroll_ladder --warp
```
