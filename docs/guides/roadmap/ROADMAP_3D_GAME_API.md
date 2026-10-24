# Roadmap: API de juego 3D para A500 (escena, assets, materiales, animación, física y math)

> **Cadencia de referencia: 25 fps**, no un gate universal. 50 fps no es objetivo en A500; cada demo declara su cadencia medida y sus excepciones (090/092). Medir en release y usar debug solo para depurar.


## Estado 2026-10-24 (corte G6/G7/P8)

- **G7 ampliado**: `ArticulatedBody3D` integra velocidad/inercia angular por parte, calcula inercia con termino propio y permite joints de bisagra con limites; `apply_impulse`, `apply_torque` y `set_angular_velocity` son la API de juego. La demo 092 anima cadera/pierna con limites. Sigue sin solver de contacto angular completo ni motores de joint.
- **Asset v2**: binding opcional cara->parte (`faces[].part`) almacenado como `u8[face_count]`; `ModelTable::face_part` lo expone. El renderer de escena aun pinta una malla por nodo; el binding permite construir mallas por parte fuera de la escena.
- **G6**: anadidos `sphere_obb`, `CollisionFilter`, `Trigger3D`, `trigger_obb`/`trigger_sphere` y demo `095_collision3d` con Blitter. Falta respuesta fisica de triggers y capas en broadphase.
- **P8**: 091 alambre directo (`render_wire_direct` + `retro::wire_lines`) 31,82 fps release / 27,48 debug; la ruta por `FramePlan` 24,06. 090 release 16,31; 092 silhouette 12,6; 096 silhouette de props 26,93 release (A/B cara 17,69). Las rutas de relleno de 090/092 no alcanzan 25; 091 y 096 sí. El `LineBatch` no mejora (21,24 vs 22,02) y se descartó.
Estado: **abierto**. **P7/P8**: el DDA de `convex_spans` se corrigio (avance de varios px por fila en aristas poco profundas; HOST-045/481); 087 y la variante rellena de 091 se revisaron en WinUAE. La cadencia sigue abierta: 091 alambre 24,06 fps release / 22,07 debug (1 plano/18 aristas visibles), relleno por cara 8,39 fps y silueta convexa 12,64 fps (2 cubos, doble buffer; P8 parcial). **G0 hecho** (HOST-488: `eng/api/3d.hpp` con escena retenida mínima, cámara y render; política numérica q0), **G1 parcial** (chunk `Model3D` 15, `Model3DAssetView`, `ModelTable` y cooker JSON con self-test; faltan `Assets::model`, chunks 16–19 y assets de animación/proxies), **G2 funcional** (pool, cámara, jerarquía padre/hijo, rotación Q4.12, dirty flags, render de alambre y demo `091_scene3d_nodes`; **cadencia abierta a 22,07 fps**) y **G3 parcial** (materiales de `ModelTable`, `FaceStyle` y patrón CPU; faltan sombreado, anclaje de patrón, patrón por Blitter y demo), **G6 parcial** (esfera/AABB/rayo/OBB-SAT, broadphase sweep-and-prune y sweep esfera-AABB + HOST-489; faltan triggers, capas/mascaras y demo) y **G7 parcial** (esferas, suelo, impulso, sleep, time scale y `CompoundBody3D` con masa/inercia + HOST-490/488; faltan broadphase, joints dinamicos, triggers y demo). Este documento planifica la **capa de juego** que convierte las piezas 3D existentes en un sistema fácil de usar para juegos poligonales en A500. No duplica el renderer de bajo nivel (paquete 1, [`ROADMAP_3D_BLITTER.md`](ROADMAP_3D_BLITTER.md)), ni la presentación Full/Fake y el horizonte Copper ([`ROADMAP_3D_PRESENTACION.md`](ROADMAP_3D_PRESENTACION.md)), ni la arquitectura de referencia ([`3D_GAME_ARCHITECTURE.md`](../../engine/architecture/3D_GAME_ARCHITECTURE.md)). Cubre los paquetes 2–8 de esa arquitectura (assets, escena, queries, física, animación, LOD y juego de prueba); el paquete 9 (presentación Full/Fake) tiene su propio plan.

## 0. Criterio de éxito: un juego 3D sin plomería

El contrato contra el que se juzga cada fase es que un juego pueda describir **qué** quiere sin nombrar bitplanes, registros, `Block<Tag>`, scratch por objeto ni rutas de relleno. El boceto siguiente es el destino, no código existente:

```cpp
#include <eng/api/api.hpp>   // App, Screen, assets, input, audio
#include <eng/api/3d.hpp>    // fachada 3D (objetivo)

struct MyGame {
    // Capacidades fijas: nodos y vértices por malla (no el total de la escena).
    eng::scene3d::Scene<64, 1024> scene;

    void init(eng::App& app) {
        m_room = scene.spawn(app.assets().model("room"));
        m_hero = scene.spawn(app.assets().model("hero"));
        scene.set_camera({.focal = 340, .near_z = 64});
        m_body = scene.physics().attach_box(m_hero, {.half = {8, 16, 8}, .mass = 4});
        scene.play(m_hero, app.assets().clip("idle"), eng::scene3d::Loop::Yes);
    }

    void update(eng::App& app, eng::s16 dt) {
        scene.move(m_hero, app.input().dir_x(), app.input().dir_z());
        scene.physics().step(dt);
        scene.camera_follow(m_cam, m_hero);
    }

    void render(eng::App& app) {
        // G0: tabla de modelos + color por cara. G3 sustituye el callback por materiales.
        scene.render(models, app.screen(), [&](auto id, u32 face) { return color_of(id, face); });
    }
};
```

El juego no elige `PerFace`/`PerPlane`/`Silhouette`, no ordena sólidos a mano, no recorta contra planos, no calcula `1/z` y no conoce la máscara del Blitter. La escena posee pools de capacidad fija y el motor decide la ruta con presupuesto medido.

**Estado de G0 (HOST-488)**: `eng/api/3d.hpp` implementa la escena retenida (`Scene<MaxNodes, MaxVertices>`, handles generacionales, cámara, posición/visibilidad y render por `render_solid` con painter por profundidad). El resto del boceto (jerarquía, rotación, `Assets::model`, materiales, animación y física) es el objetivo de G1–G7; el boceto no es una promesa de API congelada más allá de los nombres de la fachada.

## 1. Diagnóstico (estado 2026-10)

### 1.1 Lo que ya es sólido (reutilizar, no reimplementar)

| Área | Pieza actual | Estado |
|---|---|---|
| Núcleo de cámara, clipping y proyección | `eng/graphics/render3d.hpp` (`Camera3D`, `clip_camera_polygon`, `camera_contains_vertices`, `project_camera_vertex`, `screen_area2`); HOST-481; sondas L1-003/L1-005 | **Sólido**: near + cuatro laterales antes de dividir, proyección acotada, culling por área, sin heap y genérico sobre el escalar |
| Malla y orden de caras | `eng/core/data/mesh3d.hpp` (`MeshViewT`, `PolyMeshViewT`, normales por cara, `MeshFaceOrder<Kind>`); HOST-013/HOST-211 | **Sólido**: n-gon sin triangular, sólido convexo sin sort, parches convexos con pintor |
| Primitivas de malla | `eng/retro/mesh_primitives.hpp` (caja, prisma, tubo, cono, esfera, cápsula, platónicos); HOST-477 | **Sólido** para contenido de arranque y sondas |
| Relleno Amiga | `eng/graphics/polygon_planes.hpp`; `AmigaBackend::fill_polygons_by_plane*`, `blitter_fill_polygon_strided`, `blitter_apply_pattern`; `eng/graphics/fill_pattern.hpp`; `eng/retro/flat_shade_xor.hpp`; HOST-062/HOST-217/HOST-474 | **Sólido**: por cara, por plano, con patrón, dirty-rect, spans CPU como oráculo |
| Culling de entrada por entidad | `eng/core/math/frustum.hpp` (`YawView`, `Frustum`, `sphere_visible`); HOST-478 | **Hecho** para esfera/AABB contra volumen visible |
| Contenedor y vistas de assets | `eng/assets/uaf.hpp` (`Blob`, `Mesh`, `MeshPoly`, `MeshAssetView`, `PolyMeshAssetView`); `tools/assets/uaf-pack.ts`; HOST-012 | **Hecho** como base; el modelo completo no existe |
| Matemática fija | `fixed`, `fixed_math`, `fixed_trig`, `sinetable`, `isqrt`, `inv_sqrt`, `fast_div`, `mul_wide`/`div_wide`, `linalg`, `scalar_traits` | **Base sólida**; falta el kit de aproximaciones 3D |
| Presentación y pacing | `eng/graphics/render_pacing.hpp`, `present_controller.hpp`, `pan_window.hpp`; HOST-470/471/476; demo 080 | **Base hecha**; no hay integración con una escena 3D retenida |
| Colisión y trayectorias 2D | `eng/core/util/collision.hpp`, `broadphase.hpp`, `eng/scene/trajectory.hpp` | **Sólido en 2D**; no hay equivalente 3D |
| Especificación de assets 3D | [`MODEL3D_ASSET_FORMAT.md`](../../engine/architecture/MODEL3D_ASSET_FORMAT.md) (JSON → chunks UAF-R `Model3D`/`Material3D`/`RasterPattern`/`Animation3D`/`Collider3D`) | **Especificación**, sin runtime |

### 1.2 Huecos por área

| Área | Qué falta | Consecuencia para el juego |
|---|---|---|
| **Render de modelos** | `render_solid`/`render_mesh` son primitivas por objeto: el llamador transforma, ordena, limpia, aporta scratch y elige la ruta. `mesh_renderer.hpp` conserva una segunda ruta sin near clip. | Un juego escribe plomería por objeto y conviven dos contratos de cámara distintos |
| **Formato de archivos y carga** | Chunks 15–19, cooker JSON, loader tipado, `Assets::model` y validación de materiales/nodos/clips/proxies no existen; `MeshPoly` solo lleva geometría. | El contenido se escribe en C++; no hay pipeline de autoría ni carga desde disco |
| **Escena y modo retenido** | No hay `Scene3D`, `Node3D`, handles generacionales, transform dirty, extracción de renderables, cámara de juego ni presupuestos. `scene::World` es 2D y `composition::Scene` es display. | Cada juego reinventa la escena y el orden; el culling y el LOD no se pueden optimizar en un único sitio |
| **Modelos deformables** | No hay clips TRS de nodos, poses precocinadas, morph targets ni skinning; `graphics/anim.hpp` es animación de sprites 2D. | Un personaje articulado no se puede animar con el pipeline |
| **Color, materiales y entramado** | `Surface::fill_polygon` solo acepta un `u8 color`; los patrones viven en `polygon_planes`/backend y no llegan a `render3d`; no hay tabla de materiales ni política de sombreado plano. | El damero o entramado por cara obliga a bajar al backend; un material de asset no se puede expresar |
| **Visibilidad y LOD** | Solo hay esfera/AABB contra frustum; faltan sectores/portales, flood con presupuesto, descarte por tamaño proyectado, selector de LOD y contabilidad de objetos transformados. | Se transforman objetos invisibles y el frame se agota antes de dibujar |
| **Intersecciones, colisiones y física** | Sin broadphase 3D, raycast/sweep, esfera/plano, OBB SAT 3D, heightfield, masa/inercia, integrador, reposo ni escala temporal. [`3D_PHYSICS.md`](../../engine/architecture/3D_PHYSICS.md) tiene costes y plan, no implementación. | No hay interacción: el jugador atraviesa paredes y los proyectiles no consultan el mundo |
| **Matemática rápida** | `project_camera_vertex` divide por vértice; no hay LUT de `1/z` con interpolación, `atan2` aproximado, `inv_sqrt` runtime ni utilidad de rebase de origen. Los rangos de `Q4.12` (±8) y `Q8.8` (±128) no están encapsulados, y el clip satura a ±16384 unidades del escalar (para `Q4.12` son solo ±4.0). | El coste por vértice domina, el mundo se limita a ±128 unidades y la precisión de cámara no tiene una política validada |
| **Rutas de relleno Amiga** | La composición por objeto (máscara de silueta `S` + máscaras por plano `M_p`) está diseñada pero no implementada; no hay doble buffer del lienzo FG ni selección automática de ruta por coste. 087 mide 7,98 fps y 090 mide 15,3–16,2 fps. | El suelo de 25 fps del relleno poligonal no se cumple y el juego no puede escalar caras |
| **Ergonomía** | No hay `eng/api/3d.hpp`; las demos 3D incluyen `eng/graphics/render3d.hpp` y manejan buffers. | El 3D no cumple el criterio «un juego sin bajar al metal» de [`ROADMAP_GAME_API.md`](ROADMAP_GAME_API.md) |

### 1.3 Evaluación de conjunto

La base 3D es **más sólida que la media** de un engine casero para A500: el clip de cámara, las caras n-gon, el culling, el relleno por Blitter y las pruebas host ya están resueltos y documentados. Lo que falta no es «más rasterizador», sino la capa que convierte esas primitivas en un sistema de juego: **asset → escena retenida → material y ruta de relleno → consultas y física**. El riesgo principal es de **coste de relleno** (el suelo de 25 fps no se cumple con las demos actuales), no de corrección geométrica; el segundo riesgo es de **ergonomía**, porque la API actual expone scratch, orden y ruta al llamador. La estrategia correcta es **envolver el renderer robusto, no sustituirlo**.

## 2. Decisiones de diseño

- **Namespace y vocabulario**: `eng::scene3d` para la capa de juego (`Scene`, `NodeId`, `ModelHandle`; `MaterialId`/`BodyId` llegan en G3/G7). El renderer sigue en `eng::graphics::render3d` y la cámara en `eng::graphics::Camera3D`; la fachada reexporta el tipo de cámara y añade la escena. La cámara es **única por escena** en G0; si un juego necesita varias, se añade un pool en G2 sin cambiar el vocabulario.
- **Capacidad fija y handles generacionales**: la escena se parametriza por número de nodos y por **vértices máximos por malla** (`Scene<MaxNodes, MaxVertices>`), usa `util::Pool` con handles generacionales y scratch propio; cero heap, cero RTTI, cero excepciones, cero despacho dinámico.
- **Escalar y rangos (decisión de G0, HOST-488)**: la cámara usa `eng::coord` (`Fixed<s16,0>` en 68000, `int` en host). `Q8.8` y `Q4.12` no representan `focal`/`half_w` de un viewport de 320 px en `s16` (`from_int(160)` desborda), así que no son válidos para `Camera3D` tal como está. Las posiciones de mundo se **rebasan alrededor de la cámara** y se cuantizan a `eng::coord` al renderizar; la precisión sub-unidad de cámara queda para G8 (escala de píxel o LUT de `1/z`). No se ensancha el fixed a 32 bits (sus productos acaban en `__muldi3`).
- **Un solo renderer 3D**: `render3d` es la única ruta de juego. `mesh_renderer.hpp` queda como ruta histórica de tests hasta migrar sus consumidores; no se añade a la fachada.
- **La escena decide la ruta de relleno**: el juego declara el objeto y su material; el router interno elige `PerFace`, `PerPlane`, `Silhouette`, `StaticCopy` o `CpuSpans` según perfil y coste medido. La ruta no se expone en la API de juego.
- **Materiales de paleta, no RGB**: `FaceMaterial { u8 color; u8 pattern; MaterialFlags flags; }`; el color es índice de paleta, el patrón indexa el catálogo `FillPattern` o un `RasterPattern` cocinado, y las flags declaran `opaque`/`doubleSided`/`noCull`. La iluminación es **plano por cara** (normal + luz), sin focos ni iluminación por píxel.
- **Assets cocinados**: JSON de autoría → UAF-R big-endian validado; el runtime nunca parsea JSON ni hace `memcpy` por frame; las tablas de materiales, nodos, clips y proxies son opcionales y no se reservan si el asset no las usa.
- **Deformables por coste creciente**: primero animación de **nodos rígidos** (partes del personaje), después poses precocinadas, después **morph targets sparse**; el skinning por vértice queda condicionado a un consumidor y a una medida que lo justifique.
- **Física arcade B+C+D**: cuerpos rígidos con gravedad, esfera/OBB por SAT 3D, heightfield, un impulso por contacto, corrección posicional ligera, fricción saturada, sleep y escala temporal por actor. Sin solver iterativo, sin GJK/EPA, sin CCD y sin blandos en el primer corte.
- **Visibilidad por capas**: sector/portal con flood presupuestado → esfera/AABB de entidad → descarte por tamaño proyectado → LOD de malla → back-face/painter. Sin BSP con split, PVS por leaf ni occlusion queries en la primera versión.
- **Matemática rápida con oráculo**: todo atajo (LUT de `1/z`, `atan2`, `inv_sqrt`) declara rango y error máximo, tiene test contra la versión exacta y pasa `codegen-report` y `asm-audit` sin libcalls de 32/64 bits ni instrucciones 68020.
- **Ergonomía primero**: la fachada usa constantes con nombre, designated initializers y agregados documentados; ningún ejemplo necesita `60000u`, buffers ni registros. Si el corte vertical enseña un `#include` de bajo nivel, la abstracción ha fallado.

## 3. Piezas que se congelan y piezas que se retiran

| Pieza | Decisión |
|---|---|
| `render3d` (`Camera3D`, clip, proyección, `render_solid`/`render_mesh`) | **Se conserva** como núcleo; la escena lo envuelve y no lo duplica |
| `mesh3d` (`MeshViewT`, `PolyMeshViewT`, `MeshFaceOrder`) | **Se conserva** como modelo de malla |
| `mesh_renderer.hpp` | **Se retira de la ruta de juego**; se mantiene hasta migrar los tests que lo usan |
| `object3d`/`lib3d`/`obj2c` | **Se conserva** como formato histórico y oráculo; el asset nuevo no depende de él |
| `polygon_planes` + backend de relleno | **Se conserva** como motor; la escena elige la ruta |
| `fill_pattern`/`blitter_apply_pattern` | **Se conserva** y se conecta a materiales |
| `frustum.hpp` | **Se conserva**; se integra en la escena y se amplía con LOD/sectores |
| `collision.hpp`/`broadphase.hpp` 2D | **Se conserva** para 2D; el 3D usa cabeceras nuevas, no finge `z=0` |
| `scene/trajectory.hpp` 2D | **Se generaliza** a `Vec<3,S>` sin duplicar spline |
| `scene::World`/`composition::Scene` | **No se reutilizan como escena 3D**: son 2D y display respectivamente |

## 4. Fases

| Fase | Objetivo | Entregable | Verificación | Depende de |
|---|---|---|---|---|
| **G0 — Contrato y auditoría** | Fijar nombres, capacidades, propiedad y límites de la fachada; cerrar la frontera con el renderer y la política numérica | `eng/api/3d.hpp`: `Scene<MaxNodes, MaxVertices>`, `NodeId` generacional, `ModelHandle`, cámara `Camera3D<eng::coord>`, alta/baja/posición/visibilidad y render por `render_solid`; **HECHO (HOST-488)** | HOST-488: ciclo de vida, capacidad, cámara validada, painter entre nodos, recorte al viewport y omisión de modelos fuera de rango/tamaño; sonda numérica que fija **q0** y demuestra que Q8.8/Q4.12 no representan `half_w=160` en `s16` | — |
| **G1 — Asset 3D cocinado y carga** | Convertir el JSON de autoría en chunks UAF-R y cargarlo sin parseo en runtime | **PARCIAL**: chunk `Model3D=15` v1 (cabecera 28 B, vértices `s16`, índices, caras n-gon y materiales embebidos), `eng::assets::Model3DAssetView` con validación de offsets/rangos, `eng::scene3d::ModelTable` de capacidad fija y cooker `tools/assets/cook-model3d.ts` con validación de caras convexas y self-test; **HOST-488**. Pendientes: `Assets::model(name)`, chunks `Material3D=16`/`RasterPattern=17`/`Animation3D=18`/`Collider3D=19` del esquema completo y sonda L1 | Round-trip host del chunk y self-test del cooker (mismo layout); rechazos de payload truncado, índice/cara fuera de rango, tabla llena y malla mayor que `MaxVertices`; presupuesto de bytes Chip/Fast | G0 |
| **G2 — Escena 3D retenida** | Completar la escena: jerarquía, rotación, transforms sucios y extracción | **FUNCIONAL (HOST-488 + demo 091)**: `Node3D` con posición + rotación Q4.12, `parent` + `set_parent` con rechazo de ciclos, caché world, `mark_dirty`/`update_transforms`, `render_wire` con aristas únicas, doble buffer y demo `091_scene3d_nodes` (2 cubos, jerarquía, HUD) | HOST-488: composición de transforms contra oráculo, dirty propagado, orden estable y rechazo de ciclos; demo con visión OK; **cadencia ABIERTA: 22,07 fps/6,37 campos** (24 aristas × 4 planos, 96 jobs) → deuda P8/P5; flicker **SIN VERIFICAR** | G0, G1 |
| **G3 — Materiales, sombreado plano y entramado** | Modelo de material de asset y política de sombreado; la cara tiene color + patrón + flags sin bajar al backend | **PARCIAL**: `ModelMaterial`/`ModelTable` (color/patrón por cara) y render con materiales sin callback (`Scene::render(ModelTable, Surface)` + `FaceStyle` + patrón CPU; HOST-488). Pendientes: `ShadePolicy` (normal + luz → índice), anclaje de patrón objeto/cara/pantalla, agrupación por material y patrón por Blitter; demo `092_material_dither_faces` | HOST-488: color y patrón del material llegan al relleno y el damero CPU alterna; falta la demo de visión y el A/B de Blitter | G2, P7 |
| **G4 — Animación rígida y deformables** | Animar partes y, después, deformar sin comprometer el presupuesto | Runtime de `Animation3D`: clips por ticks, tracks TRS, loop/one-shot, eventos; partes rígidas por jerarquía; poses precocinadas; morph targets sparse; rig rígido (un hueso por vértice) y skinning solo si la medida lo justifica | HOST-XXX: interpolación vs oráculo, loop/eventos, delta morph, bounds por clip; demo `093_rigid_anim`; coste por objeto visible medido | G2, G1 |
| **G5 — Visibilidad y LOD** | Dejar de transformar lo invisible | `Sector`/`Portal`, flood con `max_rooms`/profundidad, rect de portal + clip, entidades de cada sector contra volumen visible, descarte por tamaño proyectado, selector de LOD con histéresis, contador de objetos transformados vs totales | HOST-XXX: grafo de sectores, flood conservador, culling contra oráculo; demo `094_rooms_portals`; métrica «objetos que nunca llaman a `transform_vertices`» | G2 |
| **G6 — Queries, colisiones y trayectorias** | Raycast, sweep y contacto para jugador, armas y objetos | **PARCIAL (HOST-489)**: `collision3d.hpp` con esfera-esfera, esfera-AABB, AABB-AABB, rayo-AABB y rayo-esfera (genérico, sin `sqrt` salvo el rayo-esfera). Pendientes: OBB/SAT 3D, `SpatialHash3D`, sweep de cápsula, heightfield, `trajectory` 3D, capas/máscaras y triggers; demo `095_collision3d` | HOST-489 con `float` y `Fixed<s16,4>`; falta SAT contra oráculo, sweep sin túnel y sonda L1 | G2 |
| **G7 — Física arcade** | Integración, impulso, reposo y tiempo lento | **PARCIAL (HOST-490)**: `eng/sim/physics3d.hpp` con cuerpos esféricos, gravedad, suelo, un impulso por contacto, corrección posicional, fricción, `sleep` y `time_scale`; pool de capacidad fija. Pendientes: broadphase 3D, OBB, triggers, masa/inercia cocinada, escala temporal por actor/grupo y demo `096_physics_boxes` | HOST-490 con `float` y `Fixed<s16,8>`: caída/reposo, cuerpo estático, par que se separa y rebota, `time_scale=0`; falta la medida de coste/codegen en 68000 y la demo | G6 |
| **G8 — Matemática 3D rápida** | Quitar divisiones y funciones caras del bucle sin ocultar defectos; cerrar la política de rango | `eng/core/math/fast3d.hpp` o equivalente: `1/z` por LUT + interpolación, `atan2` aproximado, `inv_sqrt` runtime por tabla, `sin`/`cos` en vueltas y radianes, `sqrt` fija, `div` por constante y helpers de rebase; `Camera3D::project` con `1/z`; saturación/límite de cámara parametrizable si G0 lo pide; error máximo documentado | HOST-XXX con dos escalares y error acotado; sonda de codegen sin `__mulsi3`/`__udivsi3`/`__muldi3`; A/B del coste de proyección; `asm-audit` en 68000; veredicto de rango (q0 vs Q8.8 vs Q4.12) | G0 |
| **G9 — Corte vertical 3D** | Probar que la fachada basta para un juego real | `games/401_3d_room` (siguiente libre; si se reserva un bloque 600 para 3D, actualizar [`NUMBERING.md`](../../ai-dev-environment/NUMBERING.md)): habitación, jugador, props, cámara, colisión, materiales, HUD y presentación Full/Fake; toda la lógica por `eng/api/3d.hpp` | `build -> run -> analyze`, `measure-fps`, secuencia de visión, flicker 0, `demo-ergonomics`/`prior-art`/`demo-docs`; **cero includes de `render3d`/backend en el código del juego**; base ≥12,5 Hz y display ≥25 Hz | G2–G8 |

### Dependencias

```text
G0 contrato
 ├─ G1 assets ── G2 escena ── G4 animación ──────────────┐
 │                  ├─ G5 visibilidad/LOD ───────────────┤
 │                  ├─ G6 colisiones/trayectorias ── G7 física ──┤
 │                  └─ G3 materiales (con P7) ───────────┤
 ├─ G8 math rápida ─────────────────────────────────────┘
 └─ P7/P8 del renderer (materiales y rutas de relleno) ──┘
                                                        G9 corte vertical
```

**Primer corte jugable** (sin esperar a deformables ni física completa): G0 + G1 + G2 + G3 + G6 (esfera/OBB y raycast) + G9 con una habitación estática, jugador-cápsula y props. Ese corte valida la ergonomía, el coste y el pipeline de assets antes de añadir animación, LOD y física dinámica.

## 5. Alcance excluido (para no inflar el sistema)

- **Texturas RGB y UV por píxel**: el material es índice de paleta + patrón 1 bit; una textura indexada sería otro codec con presupuesto explícito de Chip y un consumidor real.
- **Z-buffer, rasterizador de triángulos por hardware, subpíxel y antialiasing**: el painter + caras convexas es el contrato; el subpíxel sigue en F6 de la presentación, no aquí.
- **Iluminación por píxel, sombras, focos y materiales PBR**: el sombreado es plano por cara; se hornea en el asset o se elige por normal/luz.
- **ECS general, reflexión, scripting y editor visual**: la escena es un grafo de nodos de capacidad fija, no un framework.
- **BSP con split, PVS por leaf, occlusion queries y Hi-Z**: fuera del presupuesto de un 68000; sectores/portales con flood acotado es el techo.
- **GJK/EPA, CCD, cuerpos blandos, telas, fluidos y restricciones articuladas**: la física es arcade B+C+D; se documentan los límites (pilas de 2, sin túnel a velocidad acotada).
- **Skinning general por vértice**: solo si un caso visual concreto no se resuelve con partes rígidas o poses precocinadas y la medida lo justifica.

## 6. Presupuesto y rendimiento

| Escenario | Medida actual | Objetivo | Nota |
|---|---|---|---|
| 087 triángulos (7 caras) | 7,98 fps (6,27 campos) | ≥25 fps (suelo de relleno) | El coste está en 4 copias por plano del relleno por cara; la ruta debe cambiar, no el renderer |
| 088 cubo con clip y doble buffer | 40,02–48,34 fps, 1,147 campos, **inestable**; 0 ciclos CPU libres | Cadencia uniforme, sin tirones | P5 abierta; la resolución de símbolos del perfil está SIN VERIFICAR |
| 090 icosidodecaedro + cubo | 15,3–16,2 fps (3 campos) | ≥25 fps o excepción documentada | ~80 blits de línea + transform del 68000; el damero no es el cuello |
| 091 escena jerarquica (2 cubos) | 31,82 fps release / 27,48 debug con `render_wire_direct` (1 plano, 18 aristas, 1,57/1,82 campos; 222.957/258.182 ciclos); ruta por `FramePlan` 24,06/22,07; 8,39 fps relleno por cara y 12,64 silueta (deuda P8 de relleno). |
| Transform de 60 vértices | ~57k ciclos (medido) | No ejecutarlo para objetos invisibles | El culling de entrada es el multiplicador más barato |
| Escena de juego (objetivo) | — | Base ≥12,5 Hz + display ≥25 Hz con Full/Fake | Contrato de [`ROADMAP_3D_PRESENTACION.md`](ROADMAP_3D_PRESENTACION.md) |
| Física (objetivo) | — | ≤30k–60k ciclos/frame | [`3D_PHYSICS.md`](../../engine/architecture/3D_PHYSICS.md) §1 |

Reglas transversales: cero heap y cero `memset`/`memcpy` de assets en el bucle; cero `*`/`/`/`%` de 32 bits en bucles calientes; cero productos de 64 bits; buffers de scratch dimensionados por plantilla; overflow de capacidades **reportado**, no truncado silencioso.

## 7. Contrato de rutas de relleno (Amiga)

El motor de relleno y sus pruebas viven en [`ROADMAP_3D_BLITTER.md`](ROADMAP_3D_BLITTER.md) (P8). La escena solo aplica este contrato de selección:

```text
objeto visible
  ├─ geometría estática ya compuesta          → copia pre-renderizada (lo más barato)
  ├─ sólido convexo, sin solape con otro       → silueta XOR + 1 fill por plano (090)
  ├─ varios objetos que se solapan             → S de silueta + M_p por color + composición (2·planos blits)
  ├─ caras pequeñas o pocas                    → spans CPU / cookie-cut por cara (medir umbral)
  └─ objeto dinámico grande                    → doble buffer FG + clear por dirty-rect
```

Invariantes: el clear de un objeto cubre su bbox anterior y el actual; las caras cóncavas se particionan en el cocinado; el painter sustituye al z-buffer y las intersecciones entre sólidos se documentan como limitación; los patrones se aplican **después** del fill y no tocan el contorno; el Blitter asíncrono solo se usa cuando hay CPU que solapar (campaña 218).

**Memoria Chip de las máscaras**: `S` y cada `M_p` son planos 1 bpp de `row_bytes × height`; a 320×256 son 10.240 bytes por máscara. La composición mínima usa `S` + un `M_p` reutilizado por plano (2 máscaras, ~20 KB); mantener las `M_p` de todos los planos vive cuesta `planes + 1` máscaras (~51 KB con 4 planos). El doble buffer FG a 4 planos añade 2 × 40.960 bytes (~80 KB). Estas cifras entran en el presupuesto de Chip del juego antes de elegir la ruta; si no caben, se reutiliza una sola máscara o se baja a copia estática/CPU spans.

## 8. Tests y evidencia

- **Host por fase**: cada cabecera nueva entra con test host y dos escalares cuando aplique; los IDs se reservan en [`TAXONOMY.md`](../../testing/TAXONOMY.md) al crear el test.
- **On-target**: sondas L1 para carga de asset, escena, culling, colisión y física; el fallo se aísla en sonda mínima antes de tocar la demo (AGENTS §1.17).
- **Visión**: toda demo con imagen pasa por Ollama con secuencia, no una captura suelta; `VISION.md` con el prompt canónico.
- **Presupuesto**: `measure-fps.mjs`, `.amigaprofile` + IA local, `codegen-report.mjs`, `asm-audit.mjs`; se registran ciclos por sección y se comparan rutas A/B.
- **Gates**: `demo-docs`, `demo-ergonomics`, `prior-art`, `generic-headers`, `api-facade`, `links`, `encoding` y `md-wrap` en verde en la misma pasada.
- **Matriz mínima**: G0 HOST-488 (ciclo de vida, render, política q0) **en verde**; G1 HOST-488 (Model3D + ModelTable + rechazos) y self-test del cooker **en verde**; G2 jerarquía/dirty/overflow; G3 HOST-488 (materiales + patrón CPU) **en verde**, falta demo/Blitter; G4 interpolación/loop/morph; G5 flood conservador; G6 HOST-489 (esfera/AABB/rayo) **en verde**, faltan SAT/broadphase/sweep; G7 HOST-490 (caída/reposo/impulso/time scale) **en verde**, falta demo/codegen; G8 error de aproximación + codegen; G9 build/run/analyze/visión/flicker.

## 9. Riesgos y falsación

- **Coste de relleno (principal)**: si dos sólidos con cuatro planos no alcanzan 25 fps con ninguna ruta, la falsación es la medida y el fallback es copia estática, menos colores, viewport menor o excepción documentada con base ≥12,5 Hz + Fakes. No se relaja el suelo sin medida y motivo.
- **Escena retenida**: si el recorrido + extracción + orden cuesta más del 10 % del frame, se simplifica (listas estáticas, dirty spans, orden precalculado); no se convierte en un ECS general.
- **Pipeline de assets**: si el cooker se vuelve un proyecto por sí solo, el primer corte usa primitivas `constexpr` + UAF binario y se posponen nodos/clips/proxies; el formato sigue siendo el de `MODEL3D_ASSET_FORMAT.md`.
- **Deformables**: si las partes rígidas no bastan visualmente, se prueban poses precocinadas antes de skinning; el skinning por vértice no entra sin consumidor y medida.
- **Física**: si las pilas de 3+ cajas vibran, se documenta el límite de 2 y se evita apilar; no se añade un solver iterativo para maquillar la demo.
- **Rango de 16 bits**: si el mundo supera ±128 unidades, se rebasa alrededor de la cámara; si aun así no cabe, se replantea el escalar con codegen como gate, no con un `float` en el bucle. La saturación ±16384 del clip es un límite del **escalar de cámara**, no del mundo: si G0 elige `Q4.12` (±8 de rango útil), el near/focal deben caber en ±4.0 tras la saturación y se valida con una sonda antes de fijar la política.
- **Ergonomía**: si el corte vertical necesita `render3d`, scratch o `Block<Tag>` en el código de juego, G0/G2 no han cumplido y se corrige la fachada antes de añadir fases.

## 10. Demos, juegos y numeración

- Demos del ámbito `demos/techniques/amiga/3d`: **`091_scene3d_nodes`** (G2, **construida**; visión OK, 31,82 fps release con alambre directo), **`092_articulated_character`** (G7), **`095_collision3d`** (G6), **`096_silhouette_swarm`** (P8) y las pendientes 093/094/097.
- Juego del corte vertical: `games/401_3d_room` (siguiente libre; si se acuerda un bloque 600 para 3D, reservarlo en [`NUMBERING.md`](../../ai-dev-environment/NUMBERING.md) y actualizar este documento).
- Tests host: **HOST-488** cubre G0+G1 (`tests/host/graphics/488_scene3d_api`), **HOST-489** cubre colisión 3D (`tests/host/core/489_collision3d`) y **HOST-490** física arcade (`tests/host/sim/490_physics3d`); el cooker se valida con `node dist/tools/assets/cook-model3d.js --self-test`. Los nuevos reservan ID en `TAXONOMY.md`. Sondas on-target en `tests/amiga/l1_backend/` con el siguiente número libre.

## 11. Referencias

- Arquitectura: [`3D_GAME_ARCHITECTURE.md`](../../engine/architecture/3D_GAME_ARCHITECTURE.md), [`3D_RENDER_VS_PHYSICS.md`](../../engine/architecture/3D_RENDER_VS_PHYSICS.md), [`3D_PHYSICS.md`](../../engine/architecture/3D_PHYSICS.md), [`MODEL3D_ASSET_FORMAT.md`](../../engine/architecture/MODEL3D_ASSET_FORMAT.md), [`RASTER.md`](../../engine/architecture/RASTER.md), [`PUBLIC_API.md`](../../engine/architecture/PUBLIC_API.md).
- Roadmaps: [`ROADMAP_3D_BLITTER.md`](ROADMAP_3D_BLITTER.md) (renderer y rutas de relleno), [`ROADMAP_3D_PRESENTACION.md`](ROADMAP_3D_PRESENTACION.md) (Full/Fake y horizonte), [`ROADMAP_GAME_API.md`](ROADMAP_GAME_API.md) (fachada 2D de referencia), [`ROADMAP_UNIFICADO.md`](ROADMAP_UNIFICADO.md) (estado vigente).
- Técnicas: [`blitter-line-subpixel-fill.md`](../../reference/amiga/techniques/blitter-line-subpixel-fill.md), [`camera-pan-fake-frames.md`](../../reference/amiga/techniques/camera-pan-fake-frames.md), [`copper-horizon.md`](../../reference/amiga/techniques/copper-horizon.md).
- Código y evidencia: [`3d.hpp`](../../../engine/include/eng/api/3d.hpp) (fachada G0/G2), [`model3d.hpp`](../../../engine/include/eng/assets/model3d.hpp) (chunk `Model3D` v1), [`model_table.hpp`](../../../engine/include/eng/scene3d/model_table.hpp) (registro G1), [`cook-model3d.ts`](../../../tools/assets/cook-model3d.ts) (cooker), [`face_style.hpp`](../../../engine/include/eng/graphics/face_style.hpp) (G3/P7), [`render3d.hpp`](../../../engine/include/eng/graphics/render3d.hpp) (`CameraView`, `render_wire`), [`mesh3d.hpp`](../../../engine/include/eng/core/data/mesh3d.hpp), [`frustum.hpp`](../../../engine/include/eng/core/math/frustum.hpp), [`collision3d.hpp`](../../../engine/include/eng/core/util/collision3d.hpp) (G6), [`physics3d.hpp`](../../../engine/include/eng/sim/physics3d.hpp) (G7), [`polygon_planes.hpp`](../../../engine/include/eng/graphics/polygon_planes.hpp), [`fill_pattern.hpp`](../../../engine/include/eng/graphics/fill_pattern.hpp), [`uaf.hpp`](../../../engine/include/eng/assets/uaf.hpp); HOST-012/013/046/062/211/217/474/477/478/481/488/489/490; L1-003/L1-005; demos 087/088/089/090/091.
