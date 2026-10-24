# Referencia de la API 3D (estado actual)

Esta referencia cubre las piezas **publicas y estables para prototipos** de la API 3D. El objetivo de las demos 3D es **25 fps** en A500; 50 fps no es un objetivo de juego. La API aun puede evolucionar: lo marcado como **experimental** no debe considerarse congelado.

Cabeceras principales:

```cpp
#include <eng/api/3d.hpp>          // Scene, ModelTable, render
#include <eng/assets/model3d.hpp>  // asset Model3D v1/v2
#include <eng/graphics/render3d.hpp> // Camera3D, render_solid/wire/silhouette
#include <eng/sim/compound_body3d.hpp>
#include <eng/sim/articulated_body3d.hpp>
#include <eng/sim/model_body3d.hpp>
#include <eng/core/util/collision3d.hpp>
```

## Escena de juego

| Tipo | Cabecera | Uso |
|---|---|---|
| `eng::scene3d::Scene<MaxNodes, MaxVertices, MaxEdges>` | `eng/api/3d.hpp` | Escena retenida: pool de nodos, jerarquia, camara, painter y render. |
| `eng::scene3d::NodeId` | `eng/api/3d.hpp` | Handle generacional de nodo; no es un puntero. |
| `eng::scene3d::Node3D` / `NodeDesc` | `eng/api/3d.hpp` | Posicion local (`eng::coord`), rotacion Q4.12, padre, visibilidad y cache world. |
| `Scene::spawn/remove/set_parent/set_position/set_rotation/set_visible` | | Construccion y edicion de la escena. |
| `Scene::render(models, surface)` | | Pinta por cara con el color/patron del material; painter de lejos a cerca. |
| `Scene::render_silhouette(models, surface)` | | Ruta P8: una silueta convexa por objeto, 1 fill por objeto. Es una ruta **LOD/flat-fill** para props de un color, sombras, seleccion o siluetas de lejania; pierde caras, patrones y detalle. No es la ruta por defecto de un personaje con materiales. |

| `Scene::render_wire(models, surface, color, plan, op, cull_backfaces, silhouette_only)` | | Alambre con clip near/lateral; las dos ultimas opciones son optimizaciones P8. |
| `Scene::collect_wire_edges(models, out, cap, color, ...)` + `retro::wire_lines(backend, planes, ...)` | | Alambre de juego sin `FramePlan`: proyecta aristas a `WireScreenEdge` y las traza con `retro::wire_lines` (1 preludio + N líneas). Es la ruta rápida: 091 mide 30,6 fps release. |
| `Scene::update_transforms()` | | Recompone solo nodos sucios; se llama dentro de los render. |

Ejemplo minimo:

```cpp
eng::scene3d::Scene<16, 8> scene;
eng::scene3d::Camera cam {};
scene.set_camera(cam);
const auto node = scene.spawn({.model = handle, .position = eng::math3d::vec3(0, 0, 320)});
scene.set_rotation(node, rot_q12);
scene.render(models, surface);
```

## Modelos, materiales y asset

| Tipo | Cabecera | Uso |
|---|---|---|
| `eng::scene3d::Mesh` | `eng/scene3d/types.hpp` | Vista n-gon convexa sobre `eng::coord`. |
| `eng::scene3d::ModelHandle` | `eng/scene3d/types.hpp` | Indice compacto a un modelo registrado. |
| `eng::scene3d::ModelTable<MaxModels, MaxVertices, MaxIndices, MaxFaces, MaxMaterials, MaxSpheres, MaxParts>` | `eng/scene3d/model_table.hpp` | Copia el asset a buffers fijos; `MaxSpheres`/`MaxParts` habilitan fisica. |
| `ModelTable::add(asset)` / `add_mesh(mesh, color, pattern)` | | Carga un asset v2 o una primitiva procedural. |
| `ModelTable::color/pattern/material/face_part/sphere/part` | | Material, binding cara->parte y metadatos de fisica. |
| `eng::assets::Model3DAssetView` | `eng/assets/model3d.hpp` | Vista validada del chunk `Model3D` v1/v2. |
| `Model3DAssetView::bounds/sphere/part/face_part` | | AABB, esferas, partes y binding cara->parte. |
| `eng::assets::ModelMaterial` | `eng/assets/model3d.hpp` | Color de paleta, patron y flags. |
| `eng::assets::ModelPart` | `eng/assets/model3d.hpp` | Padre, pivote, rotacion, masa, densidad, friccion, restitucion y AABB local. |
| `eng::assets::ModelSphere` | `eng/assets/model3d.hpp` | Centro, radio, parte y flags. |

Model3D **v2** (64 B de cabecera) anade bounds, partes y esferas; v1 sigue siendo valido. El cooker `tools/assets/cook-model3d.ts` emite v2 si el JSON trae `parts`. Para cargar fisica en `ModelTable`, instanciar con `MaxSpheres`/`MaxParts` mayores que cero.

## Camara y renderer de bajo nivel

| Tipo/funcion | Uso |
|---|---|
| `eng::graphics::Camera3D<S>` | Focal, centro, near y semiplanos laterales; camara en el origen mirando `+z`. |
| `eng::graphics::CameraView<S>` / `world_to_camera` | Vista mundo -> camara con rotacion y traslacion. |
| `render_solid_styled(mesh, model, cam, surface, style_of, ...)` | Caras n-gon con `FaceStyle` (color + patron + flags) y culling. |
| `render_solid(mesh, model, cam, surface, color_of, ...)` | Atajo de color por cara. |
| `render_wire(...)` | Aristas unicas con clip near/lateral. |
| `render_convex_silhouette(mesh, model, cam, surface, color, ...)` | Silueta de solido convexo (1 fill). |
| `screen_area2`, `camera_contains_vertices`, `solid_depth_key` | Utilidades del pipeline. |

Convencion: `+x` derecha, `+y` abajo en pantalla, `+z` delante de la camara. El culling usa el area firmada proyectada; las mallas deben venir con winding CCW visto desde fuera.

## Relleno y presentacion

| Tipo | Uso |
|---|---|
| `eng::playfield::Surface` | Contexto de dibujo con clip; `fill_polygon`, `draw_line`, `draw_text`. |
| `eng::amiga::PolygonFillService` | Sink de relleno por Blitter (mascara 1 bpp + cookie-cut). |
| `eng::playfield::kBlitterRaster` | Enruta rect/fill al Blitter; usar en demos de juego. |
| `eng::graphics::FaceStyle` | Color + patron CPU + flags de cara. |
| `eng::graphics::FillPattern` | Patrones de cobertura 1 bpp. |

Para un juego, el lienzo debe tener un `PolygonFillService` instalado y el rasterizador Blitter; el renderer CPU existe para host/tests y como fallback.

## Fisica y colision

| Tipo/funcion | Cabecera | Uso |
|---|---|---|
| `eng::sim3d::Body3D<S>` / `PhysicsWorld3D<S, N>` | `eng/sim/physics3d.hpp` | Cuerpos esfericos arcade: gravedad, suelo, impulso, sleep y time scale. |
| `eng::sim3d::CompoundBody3D<S, MaxSpheres>` | `eng/sim/compound_body3d.hpp` | Varias esferas con masa, centro de masas e inercia diagonal; `overlaps`. |
| `eng::sim3d::ArticulatedBody3D<S, MaxParts, MaxSpheres>` | `eng/sim/articulated_body3d.hpp` | Cadena con pivotes, masa, inercia real, estado angular, joints de bola, bisagra y telescópico (`set_hinge`/`set_slide`). |
| `build_compound_from_model(view, body)` | `eng/sim/model_body3d.hpp` | Asset v2 -> cuerpo compuesto. |
| `build_articulated_from_model(view, body)` | `eng/sim/model_body3d.hpp` | Asset v2 -> cuerpo articulado. |
| `collision3d::Sphere3/Aabb3/Obb3/Ray3` | `core/util/collision3d.hpp` | Primitivas y tests. |
| `sphere_overlap`, `sphere_aabb`, `sphere_obb`, `aabb_overlap`, `obb_overlap`, `ray_aabb`, `ray_sphere`, `sweep_sphere_aabb`, `Heightfield3D`/`sphere_heightfield` | | Consultas sin heap. |
| `CollisionFilter`, `Trigger3D`, `trigger_obb`, `trigger_sphere` | | Capas/mascaras y triggers OBB sin respuesta fisica. |
| `SweepBroadphase3D<S, N>` | | Pares de AABB por sweep-and-prune en X. |

El solver angular completo (velocidad/inercia angular por parte y joints con limites) es la siguiente fase; el PBD actual resuelve posiciones y conserva pivotes.

## Errores y convenciones

- Sin heap, sin RTTI, sin excepciones en runtime.
- Capacidades fijas; `add/spawn` devuelven handles invalidos si no caben.
- Los assets y las tablas deben sobrevivir a la vista/render.
- Los colores son indices de paleta; no hay RGB por pixel.
- Medir siempre en **release** para el presupuesto de 25 fps y en debug para depurar.

## Ver tambien

- [`3D_GAME_ARCHITECTURE.md`](3D_GAME_ARCHITECTURE.md)
- [`3D_PHYSICS.md`](3D_PHYSICS.md)
- [`MODEL3D_ASSET_FORMAT.md`](MODEL3D_ASSET_FORMAT.md)
- [`ROADMAP_3D_GAME_API.md`](../../guides/roadmap/ROADMAP_3D_GAME_API.md)
- Tutorial practico: [`3D_GAME_TUTORIAL.md`](../../guides/3D_GAME_TUTORIAL.md)
