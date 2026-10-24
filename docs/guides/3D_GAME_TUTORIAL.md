# Tutorial: hacer un juego 3D en A500 con este engine

Objetivo de cadencia: **25 fps**. El engine no persigue 50 fps en 3D; el presupuesto se mide en release y las demos usan Blitter para relleno y lineas.

Este tutorial tiene dos caminos:

1. **Primitivas procedurales** (`retro::fill_box`/`fill_prism`/`fill_platonic`), ideal para prototipos y enemigos simples.
2. **Modelos cocinados** (`Model3D` v2 en UAL-R), para assets con materiales, partes y fisica.

La referencia completa de tipos esta en [`3D_GAME_API_REFERENCE.md`](../engine/architecture/3D_GAME_API_REFERENCE.md).

## 1. Esqueleto de una app 3D

```cpp
#include <eng/api/api.hpp>
#include <eng/api/3d.hpp>
#include <eng/platform/amiga/gfx3d.hpp>
#include <eng/platform/amiga/polygon_fill.hpp>
#include <eng/retro/mesh_primitives.hpp>

struct Game {
    eng::scene3d::Scene<16, 8> scene;
    eng::scene3d::ModelTable<8, 8, 24, 6, 2> models;

    void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
        // display, camara y sink de relleno Blitter
        scene.set_camera({});
    }
    void update(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
        // 1. simular
        // 2. limpiar/pintar con Scene
        // 3. HUD
    }
};
```

La escena no posee el display: la app compone los bitplanes con `composition::Scene` y le pasa una `Surface` al render. El Blitter se activa con `PolygonFillService` + `set_polygon_fill_sink` + `kBlitterRaster`.

## 2. Camino A: cubo procedural y relleno Blitter

```cpp
retro::MeshData<eng::coord, 8, 24, 6> cube;
retro::fill_box(cube, eng::math3d::vec3(20, 20, 20));
const auto handle = models.add_mesh(cube.view(), /*color*/3u);

const auto node = scene.spawn({.model = handle,
                               .position = eng::math3d::vec3(0, 0, 320)});
scene.set_rotation(node, math3d::Mat3<>::identity()); // o load_rotate

eng::playfield::Surface world { canvas, {0, 0, 320, 208} };
const u32 faces = scene.render(models, world); // painter + Blitter
```

`fill_box`/`fill_prism`/`fill_platonic` generan caras convexas con winding correcto. Para un solido de un solo color, `scene.render_silhouette(models, world)` hace **un fill por objeto** y suele ser mas barato.

## 3. Camino B: modelo cocinado

El JSON de autoria se convierte a un chunk `Model3D` con `cook-model3d`:

```bash
node dist/tools/assets/cook-model3d.js assets/hero.json build/hero.uafr
```

Un JSON minimo con partes y esferas:

```json
{
  "format": "amiga-model-source",
  "version": 2,
  "vertices": [[-20,-20,-20],[20,-20,-20],[20,20,-20],[-20,20,-20],
               [-20,-20,20],[20,-20,20],[20,20,20],[-20,20,20]],
  "faces": [
    {"vertices":[4,5,6,7],"material":0},
    {"vertices":[0,3,2,1],"material":0},
    {"vertices":[7,6,2,3],"material":0},
    {"vertices":[0,1,5,4],"material":0},
    {"vertices":[1,2,6,5],"material":0},
    {"vertices":[0,4,7,3],"material":0}
  ],
  "materials": [{"color":3,"pattern":0}],
  "parts": [
    {"parent":null,"pivot":[0,0,0],"mass":100,"density":1.0,
     "friction":0.8,"restitution":0.1,
     "spheres":[{"center":[0,0,0],"radius":20}]}
  ]
}
```

Carga en runtime:

```cpp
eng::assets::Model3DAssetView view {payload};
eng::scene3d::ModelTable<8, 8, 24, 6, 2, 4, 2> table; // ojo: MaxSpheres/MaxParts
const auto handle = table.add(view); // false/invalido si no cabe
```

Para fisica:

```cpp
eng::sim3d::CompoundBody3D<eng::coord, 4> body;
if (eng::sim3d::build_compound_from_model(view, body)) {
    body.position = eng::math3d::vec3(0, 40, 320);
}
```

`CompoundBody3D` calcula masa, centro de masas e inercia diagonal a partir de las esferas. `ArticulatedBody3D` agrupa partes con pivotes y joints PBD; su `step` integra gravedad y mantiene los pivotes.

## 4. Camara, orientacion y painter

- La camara esta en el origen y mira `+z`.
- `position` es el centro del modelo en world; la rotacion es Q4.12.
- El painter ordena objetos por `z` de la posicion; dos solidos que se **interpenetran** pueden requerir separarlos o una ruta por silueta/mascaras.
- Para caras visibles, el winding debe ser **CCW visto desde fuera**.
- `Camera3D` usa `eng::coord` (q0); precision subpixel de camara queda para una fase posterior.

## 5. Bucle de juego y presupuesto

```cpp
void update(...) {
    // simulacion a tick fijo (1/25 o 1/50) con time_scale
    body.step(dt, gravity, ground_y);
    scene.set_position(node, body.position);

    // render: limpiar solo lo necesario, luego painter de lejos a cerca
    scene.render(models, world);
    hud.draw(...);
}
```

Presupuesto orientativo de un frame 3D en A500:

| Elemento | Coste |
|---|---|
| Transform + culling de pocos solidos | barato; no transformar lo invisible |
| Relleno por cara con Blitter | dominante; usar silueta si el color es plano |
| Lineas de alambre | dominante por job; cull de traseras y 1 plano |
| Fisica de esferas/partes | bajo si N es pequeno y hay broadphase |
| HUD/overlay | bajo |

Reglas: medir en **release**; no dibujar planos que no aportan color; no usar el raster CPU en gameplay; no poner 50 fps como objetivo.

## 6. Errores que ya estan resueltos (y que no hay que reintroducir)

- El relleno `convex_spans` necesita avanzar varias filas/pixeles en aristas poco profundas; el bug de muescas esta cubierto por HOST-045/HOST-481.
- El culling por area asume `y` hacia abajo: CCW desde fuera = area negativa.
- El wireframe usa aristas unicas y clip near/lateral; no proyectar vertices detras de la camara.
- Los assets v2 con fisica necesitan capacidades `MaxSpheres/MaxParts` en `ModelTable`.

## 7. Siguiente nivel

- Anadir joints angulares y animacion de partes cuando se cierre G7.
- Usar `verify-3d-sequence.mjs` y vision local para validar contornos y flicker en cada demo.
- Medir 087/090/091/092 antes y despues de cada cambio de ruta de relleno.
