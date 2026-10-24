# Tutorial: hacer un juego 3D en A500 con este engine

Cadencia de referencia: **25 fps**, no un mínimo universal. El engine no persigue 50 fps en 3D; el presupuesto se mide en release y cada demo documenta su excepción si no llega.

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

## 5.1 Alambre rápido sin FramePlan

Para alambre de juego, `Scene::collect_wire_edges(...)` proyecta las aristas a `WireScreenEdge` y `retro::wire_lines(...)` las traza; evita el `FramePlan`/`Surface::draw_line` y fue la diferencia entre 24 y 30,6 fps release en 091. Úsalo cuando el color sea uniforme; si necesitas varias capas o mezclar con otros blits, usa `FramePlan` y mide. Un `LineBatch` de jobs no mejora el trazado: el coste está en cada línea del Blitter, no en el alta del job.

## 6. Errores que ya estan resueltos (y que no hay que reintroducir)

- El relleno `convex_spans` necesita avanzar varias filas/pixeles en aristas poco profundas; el bug de muescas esta cubierto por HOST-045/HOST-481.
- El culling por area asume `y` hacia abajo: CCW desde fuera = area negativa.
- El wireframe usa aristas unicas y clip near/lateral; no proyectar vertices detras de la camara.
- Los assets v2 con fisica necesitan capacidades `MaxSpheres/MaxParts` en `ModelTable`.

### Joints baratos: bisagra y telescópico

`ArticulatedBody3D::set_hinge(part, axis, min_turns, max_turns)` limita el giro a un eje; `set_slide(part, axis, min_distance, max_distance)` convierte el pivote hijo en un desplazamiento lineal con topes. Ambos son restricciones PBD: no añaden un solver de contactos. Para un personaje, la bisagra cubre rodilla/codo; el telescópico cubre un pistón, una plataforma o un brazo extensible. Mantén estos grados de libertad separados de la cinemática de la pose y del contacto rígido.

## 7. Siguiente nivel

- Anadir joints angulares y animacion de partes cuando se cierre G7.
- Usar `verify-3d-sequence.mjs` y vision local para validar contornos y flicker en cada demo.
- Medir 087/090/091/092 antes y despues de cada cambio de ruta de relleno.


## Gate de secuencias 3D

Para CI on-target, captura al menos 3 frames y valida movimiento/continuidad:

```bash
node tools/check/3d-demo-gate.mjs --strict
```

El gate revisa 090, 091, 092 y 095 y falla si falta una secuencia o si no hay cambio entre el primer y el ultimo frame. Sin `--strict` (host CI) avisa y omite las secuencias ausentes. Para depurar un fallo usa `tools/analyze/verify-3d-sequence.mjs <dir> --require-motion`; la vision sigue siendo el gate visual final.
## 8. Rendimiento

Las rutas medidas y la política de física desacoplada están en [3D_HIGH_PERFORMANCE_GAMEPLAY.md](3D_HIGH_PERFORMANCE_GAMEPLAY.md). Regla corta: alambre directo con collect_wire_edges + wire_lines, silueta solo para props planos, física en el hueco del Blitter a >=5 Hz y fixed q0/q12 con escalas explícitas.
