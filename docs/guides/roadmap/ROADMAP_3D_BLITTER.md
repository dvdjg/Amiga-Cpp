# Roadmap: librería 3D acelerada por Blitter (renderer robusto + pila de tests)

Plan para una **librería de gráficos 3D** de dominio (`eng::graphics::render3d`) que renderiza sólidos poligonales en A500 con el **relleno de caras por Blitter** ya existente, con un pipeline **robusto** (cámara, near/frustum clipping, viewport, buffers acotados) y una **pila de tests creciente**: triángulos en movimiento → pentágonos → poliedros rotando y entrando/saliendo por las cuatro esquinas. Es el **paquete 1** (renderer 3D) de [`3D_GAME_ARCHITECTURE.md`](../../engine/architecture/3D_GAME_ARCHITECTURE.md); la presentación Full/Fake tiene su propio plan en [`ROADMAP_3D_PRESENTACION.md`](ROADMAP_3D_PRESENTACION.md) y no se duplica aquí. La **capa de juego** (assets, escena retenida, materiales, animación, visibilidad, colisiones y física) se planifica en [`ROADMAP_3D_GAME_API.md`](ROADMAP_3D_GAME_API.md): este documento se queda con el renderer de bajo nivel y las rutas de relleno.

## Estado de partida (piezas que ya existen: no reimplementar)

| Pieza | Dónde | Estado |
|---|---|---|
| Relleno de polígono **por cara** con Blitter (máscara 1 bit + contorno `ONEDOT` + area fill **inclusivo** + cookie-cut por plano) | `AmigaBackend::blitter_fill_polygon[_strided]` ([`backend.hpp`](../../../engine/include/eng/platform/amiga/backend.hpp)), puente `eng::amiga::PolygonFillService` y hook `Playfield::set_polygon_fill_sink` | Validado en hardware (ruta B de la demo 116) y por el sink host HOST-062; **sin demo viva** desde que se retiró la suya |
| Relleno **compuesto por plano** (contorno XOR + area fill, 1 fill por plano) | `graphics::fill_polygons_by_plane` + `polygon_planes.hpp` | Verificado por la demo 203 y el self-test de 077 |
| Mallas: vértices/índices, n-gon (`PolyMeshViewT`), transform afín, culling por normal almacenada y painter por cara | `eng/core/data/mesh3d.hpp` (`mesh_transform`, `mesh_patches_order_lit`), HOST-011/013/014/046 | Hecho |
| Primitivas 3D `constexpr` (caja, prisma, tubo, cono, esfera, cápsula, platónicos) | `eng/retro/mesh_primitives.hpp` | Hecho (HOST-477) |
| Proyección en perspectiva 4.12 sin 64 bits | `graphics/mesh_renderer.hpp` (`project_perspective`, `mul_wide`/`div_wide`) | Hecho |
| Clipping 2D Sutherland–Hodgman con convención de bordes corregida | `eng/retro/lib2d.hpp` (HOST-479) + `Surface::fill_polygon` (ya recorta) | Hecho |
| **Clipping de cámara (near + planos laterales) antes de dividir por z** | `graphics/render3d.hpp` (`clip_camera_polygon`, `Camera3D`) | **Hecho** (HOST-481; sondas L1-003/L1-005) |
| Renderer por objeto: transform + cull + clip + proyección + fill, buffers del llamador | `graphics/render3d.hpp` (`render_solid`, `render_mesh`, `screen_area2`, `solid_depth_key`) | **Hecho** (HOST-481); es una primitiva, no una escena |
| **Renderer de escena**: extracción de varios sólidos, painter entre objetos automático, presupuesto y contabilidad | `eng/api/3d.hpp` (`Scene<MaxNodes, MaxVertices>`) | **Hecho funcional** (G2: pool, cámara, jerarquía, rotación, dirty flags y demo 091 con 2 cubos); **cadencia abierta a 22,07 fps** (96 jobs de línea) |
| **Material por cara** (color + patrón/entramado + flags) en la ruta de render | `graphics/face_style.hpp`, `Surface::fill_polygon(..., FaceStyle)`, `Playfield::fill_polygon_pattern`/`draw_span_pattern`, `render_solid_styled` y `ModelTable::color/pattern`; HOST-488 | **Parcial**: CPU/patrón y render con materiales sí; el patrón por Blitter (sink) queda en P8/G3 |
| **Wireframe robusto** (near clip, aristas únicas, sin depender de `mesh_renderer`) | `graphics::render_wire` + `clip_camera_segment` + `WireEdge`; HOST-488; demo 091 | **Hecho** con ruta CPU y Blitter (`FramePlan`, `Copy`/`EOR`); 12 aristas del cubo sin duplicados y clip near/lateral; sin subpíxel y con capacidad de aristas del llamador |
| **Cámara de juego** (vista mundo→cámara, far plane, controlador) y **proyección con `1/z`** | `graphics::CameraView`/`world_to_camera`; HOST-488 | **Parcial**: transform mundo→cámara hecho; falta integrarla en `Scene`, el far plane y la LUT de `1/z` (G8) |
| **Composición por objeto** (silueta `S` + máscaras por plano `M_p`) y **doble buffer FG** | Diseño en este documento; no implementado | **Falta** (P8) |
| Subpíxel de aristas | [`blitter-line-subpixel-fill.md`](../../reference/amiga/techniques/blitter-line-subpixel-fill.md) §2 | Documentado, no implementado (F6 de la presentación; **no** prioritario) |

## Decisiones de diseño

- **Sin árbol nuevo ni registros**: la librería es dominio puro sobre `Surface` y las piezas anteriores. El Blitter se usa por el hook del playfield (`PolygonFillSink`); la demo instala el `PolygonFillService` con su máscara.
- **Convenio de cámara único**: espacio de cámara con `+x` derecha, `+y` abajo, `+z` delante y cámara en el origen; vértices en el escalar de coordenadas `S` (en la máquina `eng::coord` = `Fixed<s16,0>`, enteros de mundo; `real`/`Fixed<s16,12>` queda para ratios). `focal`, `near_z` y `half_w/half_h` viven en `S`; `cx/cy` son píxeles. Proyección `sx = cx + (x·focal)/z`, `sy = cy + (y·focal)/z` (coherente con `project_camera_vertex`).
- **Clipping en espacio de cámara ANTES de dividir**: near (`z ≥ near_z`) y **cuatro planos laterales** (`|x|·focal ≤ z·half_w`, `|y|·focal ≤ z·half_h`). Esto acota la proyección al viewport (±1 px de redondeo, que `Surface` recorta) y elimina los desbordes de `s16` y los contornos fuera del bitmap que corrompen el Blitter (la convención de bordes de HOST-479 sigue vigente como segunda barrera).
- **Relleno por cara opaco con painter** (objetos ordenados por profundidad y caras por `mesh_patches_order_lit`): es la ruta correcta para escenas con **solapamiento** entre triángulos sólidos u objetos distintos. El relleno compuesto por plano (paridad) queda como vía rápida documentada para geometría **disjunta** (misma clase de escena que la 203).
- **Sin z-buffer ni subpíxel**: painter + caras convexas; la robustez de bordes es «dentro del bitmap y fuera de z≤0», no exactitud subpíxel. La partición de caras cóncavas es del cocinado (no del runtime).
- **Coste**: máscara 1 bpp en Chip del llamador; un `clear` de viewport por frame (contrato del relleno por cara); cero heap; todo el álgebra del clip con `mul_wide`/`div_wide` (nunca enteros de 32×32 que acaban en `__mulsi3`).
- **Colores**: política de sombreado plano del juego (por cara a partir de la normal); la librería transporta el índice, no conoce paletas.

## Fases

| Fase | Objetivo | Entregable | Verificación |
|---|---|---|---|
| **P0 — Auditoría y presupuesto** | Piezas, contratos y coste esperado | Este documento + tabla de estado | Revisión de código citado; medidas de la 116 como referencia de peor caso |
| **P1 — Núcleo `render3d`** | Cámara, clip de cámara (5 planos), proyección, sólido (transform + cull + painter + clip + fill por `Surface`), painter entre objetos, buffers acotados | `engine/include/eng/graphics/render3d.hpp` (+ README de cabecera). **HECHO (HOST-481)** | **HOST-481** (verde): casos de near/laterales/esquinas, invariantes (toda coordenada emitida dentro del viewport; `z ≥ near`; n≥3), degenerados, orden determinista y pentágono n-gon sin triangular. Pipeline **genérico sobre el escalar** (`Camera3D<S>`/`CamVertex<S>`, `scalar_traits::mul_div`) y **fast path de frustum** por objeto (si todas las esquinas están dentro, no se recorta ninguna cara); HOST-481 instancia `float` y `Fixed<s16,0>` |
| **P2 — Triángulos** | Demo `3d/087_blit3d_triangles`: N triángulos sólidos moviéndose en 2D, solapándose y entrando/saliendo por las esquinas | Demo + INTENT/VALIDATION/VISION. **CONSTRUIDA** | Visión de secuencia OK (contornos limpios, movimiento) y presupuesto **ABIERTO**: 7,98 fps (6,27 campos). Hotspot localizado con secciones del engine: clear del área ~110k y ~60k/cara de tiempo de Blitter en las 4 copias por plano del relleno por cara (spans CPU y renderer baratos). Flicker sin declarar hasta cerrar P5 |
| **P3 — Clipping y relleno por plano** | Demo `3d/088_blit3d_planes`: cubo convexo recortado por cámara, culling, fill por plano y doble buffer; sonda estática `089_blit3d_fill_probe` para separar fill de publicación | Demos + `VALIDATION.md`; **CONSTRUIDAS** | 088 mide 40,02–48,34 fps (1,147 campos) pero **INESTABLE** y con 0 ciclos CPU libres; 089 confirma que el fill/contorno es correcto. P5 sigue abierta |
| **P4 — Poliedros** | Demo `3d/090_polyhedra_orbit`: icosidodecaedro (20 triángulos + 12 pentágonos) y cubo en órbita, colores por cara y damero en el plano de entramado | Demo + `VALIDATION.md`; **CONSTRUIDA** | Visión de secuencia OK; **15,3–16,2 fps (3 campos)**, por debajo del suelo de 25 fps. A/B sin damero 16,3 fps: el techo son los ~80 blits de línea y el transform, no el color. Cadencia aceptada por el usuario con deuda documentada |
| **P5 — Campaña de rendimiento** | Medir por secciones (clip/sort/fill), A/B de una variable, reducir blits por cara si el perfil lo pide; documentar el presupuesto | Tabla de deltas + deuda si algo queda corto | `088_blit3d_planes` parte de 31,91 fps/222.296 ciclos/frame; la ruta D (proyección de 8 vértices + recorte seguro 2D) y G (contador HUD por buckets de 8 frames) midió 43,60 fps/162.701 ciclos/frame en la re-medida con el fix HOST-479 (+36,5 % fps, −26,8 % ciclos frente al baseline). La ventana dio 40,02–48,34 fps (1,147 fields/frame) y sigue **INESTABLE**, con 0 ciclos CPU libres por field. HOST-479 normaliza la interpolación de aristas compartidas y su raster CPU coincide bit a bit con la cara-unión; la secuencia de 32 muestras cubre bordes, esquinas, colores y HUD, mientras Ollama no detecta huecos en el tramo consecutivo inspeccionado. HOST-481/L1-003/L1-005 y `--edge-stress` pasan. El perfil enumera como hotspots `clip_camera_polygon`/`clip_face_to_rect`/fill, pero su atribución PC→símbolo queda **SIN VERIFICAR**: asigna ciclos a `configure_memory` en todos los fields aunque solo se llama en init. El siguiente gate es corregir/validar la resolución de símbolos y capturar después de READY antes de elegir otro fast path. P5 sigue **ABIERTA** hasta conseguir cadencia uniforme y cumplir una excepción de presupuesto documentada o el objetivo de 50 fps. |
| **P6 — Robustez y regresión** | Fuzz host del clip con polígonos aleatorios (dentro/fuera/cruzando), secuencias de visión y flicker de las cuatro demos en el gate | Casos añadidos a HOST-481 + `VALIDATION.md` de cada demo | Suite por demo en verde (build/run/analyze/flicker), `flicker-baseline.json` declaradas |
| **P7 — Renderer para juego** | **Mecanismo** de estilo de cara en el renderer (`FaceStyle`: color + patrón + flags), wireframe con near clip y aristas únicas, ruta de líneas por Blitter (`FramePlan`) y cámara de juego (vista mundo→cámara); gancho de `1/z`, far plane y patrón por Blitter quedan en P8/G8 | **PARCIAL (HOST-488 + demo 091)**: `FaceStyle` + patrón CPU + `render_solid_styled` + `render_wire`/`clip_camera_segment` + `FramePlan`/`RasterOp` + `CameraView`; demo `093_material_dither_faces` pendiente | HOST-488 (patrón CPU vs oráculo, materiales de `ModelTable`, 12 aristas únicas, clip near/lateral y `CameraView`); demo 091 con visión de secuencia OK; falta el A/B de Blitter y la demo de materiales |
| **P8 — Rutas de relleno y presentación** | Router por objeto (`StaticCopy`/`Silhouette`/`PerPlane`/`PerFace`/`CpuSpans`), composición `S` + `M_p` para solapes, clear por dirty-rect, doble buffer del lienzo FG, publicación en VBlank y Blitter asíncrono cuando compense | Implementación + tabla de umbrales; integración en `render3d`/escena; campaña de rendimiento P5 | 087 ≥25 fps, 090 ≥25 fps o excepción documentada, 088 con cadencia uniforme; A/B de una variable por ruta; flicker 0; `VALIDATION.md` |
> Nota 2026-10-24: el DDA de `convex_spans` avanzaba como maximo 1 px por fila en aristas con `|dx| > dy`, dejando muescas en caras y cubos rellenos; corregido con `while` y cubierto por HOST-045/HOST-481. 087 y la variante rellena de 091 se revisaron en WinUAE. Ademas, la ruta de alambre paso de 96 a 18 jobs (1 plano, culling de traseras, dedup O(1) y clear asincrono): 091 mide 22,07 fps. El A/B de silueta convexa lleva el relleno de 8,39 a 12,64 fps; faltan S + M_p y el router por coste. La demo 099 añade una ruta de **spans CPU directos** para caras planas (3 planos, 3 sólidos, balas y estrellas): 19,57 fps release (362.399 ciclos/frame, 2,554 campos), por debajo del suelo de 25 fps; la silueta de dos tonos anterior medía 26,42 fps. El sombreado por caras usa normales locales rotadas y un sol común; para recuperar presupuesto hay que pasar las caras a BOBs/sprites. El `plane_or_span` del engine estrena una ruta por palabras en big-endian (host conserva la ruta por bytes; HOST-217/474/479/481/482). El router por coste del contrato P8 sigue pendiente.

## Frontera con la capa de juego

Este roadmap termina en el **renderer** y sus pruebas. La capa que convierte el renderer en un sistema de juego (asset JSON→UAF, escena retenida, materiales de asset, animación, visibilidad por sectores, colisiones y física) vive en [`ROADMAP_3D_GAME_API.md`](ROADMAP_3D_GAME_API.md). La división es deliberada: el renderer no conoce assets ni escenas, y la escena no conoce registros, máscaras ni rutas de Blitter. Los puntos de contacto son P7 (material y wireframe), P8 (selección de ruta y doble buffer) y G8 (proyección con `1/z`); la escena consume `Camera3D` y `render_solid` sin reimplementarlos.

## Solapes entre sólidos: painter por objeto con máscaras por plano (decisión 2026-10-19)

El relleno por paridad exige que, **dentro de cada pasada de plano**, los polígonos de ese plano no se solapen (dos cruces en la misma zona se cancelan). Un sólido convexo con back-face culling cumple el contrato (sus caras visibles son disjuntas), pero **dos sólidos distintos sí pueden solaparse en la proyección 2D** (uno delante de otro). Para no renunciar a la ventaja del fill por plano (O(planos) fills por objeto en vez de uno por cara) se adopta:

```text
para cada objeto (de LEJOS a CERCA):
    1) máscara S (1 bpp scratch): contorno de TODAS sus caras + fill XOR  -> silueta opaca
    2) para cada plano p: máscara M_p = caras con el bit p (contorno+fill)  -> color del objeto
    3) composición por plano:  D &= ~S   (destapa el fondo)  y  D |= M_p  (pinta el color)
                                                                 2 blits/plano
```

- Coste por objeto: `1 + planos` fills (S y M_p) + `2·planos` blits de composición; para 2 planos son 3 fills + 4 blits, frente a «1 fill por cara × planos copias» del camino por cara. Con sólidos de 6-12 caras el ahorro es grande y crece con las caras.
- **Correcto con solapes entre objetos**: S hace la oclusión opaca (no depende de la paridad del color), M_p solo se aplica donde S (el objeto tapa lo de atrás con su color o con negro si el color es 0).
- Las caras de un mismo objeto siguen exigiendo convexidad/disjunción (back-face culling); es la misma restricción ya documentada del contenido.
- La máscara S y las M_p viven en un plano 1 bpp scratch del llamador (Chip); con 4 planos hacen falta `planos+1` scratchs o reutilizar uno por plano con el orden de composición.

Se descarta por coste la alternativa de máscara por cara en escenas con muchos polígonos (la que usa la demo 087): queda como **fallback** para geometría cóncava o no disjunta.

## Descartado (2026-10-19): no era un bug del relleno por plano

La sonda estática `089_blit3d_fill_probe` (rectángulo y rombo con aristas inclinadas, `fill_polygons_by_plane`, dibujado UNA vez en `init`, 1 plano visible) sale **perfecta**: el camino contorno XOR + dedupe + `FILL_XOR` descendente es correcto, también con aristas inclinadas. El artefacto de la 088 era de **presentación**: el fill escribe de abajo arriba y una captura podía pillar el blit a medias; además, publicar el doble buffer a mitad de update deja al raster viendo un buffer en construcción (capturado una vez). Corregido el flujo a **publicar al inicio del update**; queda un caso residual intermitente a vigilar (posible parón de la familia 212 o publicación fuera de VBlank). Próximo paso: publicar el frame en el hook de VBlank (o usar `PresentController`) y revalidar la secuencia; después rotación.

## Presupuesto y estabilidad

- Efecto de relleno poligonal: **suelo 25 fps** (`AGENTS.md` §3); las demos declaran su clase y su peor caso. Triángulos pequeños deben aspirar a 50 fps (1 campo/frame).
- El coste por cara es `clear bbox + contorno (longitud de aristas) + area fill del bbox + cookie-cut por plano`; el número de caras y el área del bbox mandan. La campaña de P5 mide y decide (p. ej. caras grandes → menos caras; muchas caras pequeñas → valorar `fill_polygons_by_plane` para escenas disjuntas).
- Clear de viewport por frame (contrato del relleno por cara); la capa HUD usa la receta de franja 1 bpp existente.
- Sin picos: el clip de cámara es O(vértices·planos) con coste acotado; el painter es O(n log n) pequeño; prohibido `*`/`/` de 32 bits en los bucles (usar `mul_wide`/`div_wide`).

## Riesgos y falsación

- **Coste del relleno por cara**: si una escena a 25 fps no cabe con el número de caras pedido, la falsación es clara (medida) y el fallback documentado es la vía por plano (disjunta) o bajar caras/área; no se relaja el suelo.
- **Painter sin z-buffer**: objetos que se **intersecan** darán orden incorrecto (limitación inherente); las demos evitan intersección (entradas/salidas por esquinas en profundidades distintas).
- **Rango del escalar**: en la máquina el espacio de cámara usa `eng::coord` (`Fixed<s16,0>`, enteros de mundo) y el clip satura a ±16384; objetos muy lejos o muy cerca agotan el rango. El contrato del renderer es «cámara lejos del near» y el clip evita la división inválida; el rango útil y la política de rebase se documentan en [`ROADMAP_3D_GAME_API.md`](ROADMAP_3D_GAME_API.md) G8.
- **Subpíxel**: sin él, aristas con escalones de 1 px; aceptado (F6 de presentación) y no bloquea el suelo de fps.

## Demos y numeración

- En uso: `087_blit3d_triangles` (P2), `088_blit3d_planes` (P3), `089_blit3d_fill_probe` (sonda de fill), `090_polyhedra_orbit` (P4), `091_scene3d_nodes` (G2: alambre), `092_articulated_character` (G7: PBD + Blitter), `096_silhouette_swarm` (P8) y `099_starfox_travel` (traveling 3D, spans CPU). Las demos 094/097/098 están reservadas por los planes de visibilidad y presentación; el siguiente libre se consulta con `node tools/check/next-number.mjs demos/techniques/amiga/3d`.
- Tests host en uso: `tests/host/graphics/481_render3d_clip` (renderer) y `tests/host/graphics/478_visibility` (culling de entrada); los nuevos de P7/P8 reservan ID en [`TAXONOMY.md`](../../testing/TAXONOMY.md).

## Referencias

- Arquitectura: [`3D_GAME_ARCHITECTURE.md`](../../engine/architecture/3D_GAME_ARCHITECTURE.md) (paquete 1), [`3D_RENDER_VS_PHYSICS.md`](../../engine/architecture/3D_RENDER_VS_PHYSICS.md), [`RASTER.md`](../../engine/architecture/RASTER.md).
- Técnicas: [`blitter-line-subpixel-fill.md`](../../reference/amiga/techniques/blitter-line-subpixel-fill.md), [`flat-shade` de la 116](../../../demos/techniques/amiga/effects/116_flatshade_convex/README.md) y la decisión por cara en [`FLATSHADE_CONVEX_PORT_PLAN.md`](../../demos/effects/FLATSHADE_CONVEX_PORT_PLAN.md).
- Estado vigente del raster: [`ROADMAP_UNIFICADO.md`](ROADMAP_UNIFICADO.md) §rasterizado; presentación: [`ROADMAP_3D_PRESENTACION.md`](ROADMAP_3D_PRESENTACION.md); capa de juego: [`ROADMAP_3D_GAME_API.md`](ROADMAP_3D_GAME_API.md).
