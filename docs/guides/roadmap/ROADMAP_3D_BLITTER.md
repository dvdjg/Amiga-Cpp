# Roadmap: librería 3D acelerada por Blitter (renderer robusto + pila de tests)

Plan para una **librería de gráficos 3D** de dominio (`eng::graphics::render3d`) que renderiza sólidos poligonales en A500 con el **relleno de caras por Blitter** ya existente, con un pipeline **robusto** (cámara, near/frustum clipping, viewport, buffers acotados) y una **pila de tests creciente**: triángulos en movimiento → pentágonos → poliedros rotando y entrando/saliendo por las cuatro esquinas. Es el **paquete 1** (renderer 3D) de [`3D_GAME_ARCHITECTURE.md`](../../engine/architecture/3D_GAME_ARCHITECTURE.md); la presentación Full/Fake tiene su propio plan en [`ROADMAP_3D_PRESENTACION.md`](ROADMAP_3D_PRESENTACION.md) y no se duplica aquí.

## Estado de partida (piezas que ya existen: no reimplementar)

| Pieza | Dónde | Estado |
|---|---|---|
| Relleno de polígono **por cara** con Blitter (máscara 1 bit + contorno `ONEDOT` + area fill **inclusivo** + cookie-cut por plano) | `AmigaBackend::blitter_fill_polygon[_strided]` ([`backend.hpp`](../../../engine/include/eng/platform/amiga/backend.hpp)), puente `eng::amiga::PolygonFillService` y hook `Playfield::set_polygon_fill_sink` | Validado en hardware (ruta B de la demo 116) y por el sink host HOST-062; **sin demo viva** desde que se retiró la suya |
| Relleno **compuesto por plano** (contorno XOR + area fill, 1 fill por plano) | `graphics::fill_polygons_by_plane` + `polygon_planes.hpp` | Verificado por la demo 203 y el self-test de 077 |
| Mallas: vértices/índices, n-gon (`PolyMeshViewT`), transform afín, culling por normal almacenada y painter por cara | `eng/core/data/mesh3d.hpp` (`mesh_transform`, `mesh_patches_order_lit`), HOST-011/013/014/046 | Hecho |
| Primitivas 3D `constexpr` (caja, prisma, tubo, cono, esfera, cápsula, platónicos) | `eng/retro/mesh_primitives.hpp` | Hecho (HOST-477) |
| Proyección en perspectiva 4.12 sin 64 bits | `graphics/mesh_renderer.hpp` (`project_perspective`, `mul_wide`/`div_wide`) | Hecho |
| Clipping 2D Sutherland–Hodgman con convención de bordes corregida | `eng/retro/lib2d.hpp` (HOST-479) + `Surface::fill_polygon` (ya recorta) | Hecho |
| **Clipping de cámara (near + planos laterales) antes de dividir por z** | — | **Falta** (paquete 1) |
| **Renderer de escena**: varios sólidos, painter entre objetos, buffers acotados, color por cara | — | **Falta** |
| Subpíxel de aristas | [`blitter-line-subpixel-fill.md`](../../reference/amiga/techniques/blitter-line-subpixel-fill.md) §2 | Documentado, no implementado (F6 de la presentación; **no** prioritario) |

## Decisiones de diseño

- **Sin árbol nuevo ni registros**: la librería es dominio puro sobre `Surface` y las piezas anteriores. El Blitter se usa por el hook del playfield (`PolygonFillSink`); la demo instala el `PolygonFillService` con su máscara.
- **Convenio de cámara único**: espacio de cámara con `+x` derecha, `+y` abajo, `+z` delante y cámara en el origen; vértices en 4.12 con signo (`4096 = 1.0`). Proyección `sx = cx + (x·focal)/z`, `sy = cy + (y·focal)/z` (coherente con `project_perspective`).
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
| **P3 — Pentágonos** | Demo `3d/088_blit3d_pentagons`: pentágonos convexos (n-gon) con giro 2D y movimiento por bordes/esquinas | Demo completa | Igual que P2 + host: una cara de 5 vértices se emite sin triangular |
| **P4 — Poliedros** | Demo `3d/089_blit3d_solids`: caja/tetraedro/octaedro (o prisma pentagonal) rotando en 3 ejes, entrando y saliendo por las cuatro esquinas (cruzan bordes y el near plane) | Demo completa | Visión por secuencia (rotación perceptible, caras correctas, culling sin caras invertidas), fps, flicker, gates |
| **P5 — Campaña de rendimiento** | Medir por secciones (clip/sort/fill), A/B de una variable, reducir blits por cara si el perfil lo pide; documentar el presupuesto | Tabla de deltas + deuda si algo queda corto | `measure-fps` + perfil; sin telemetría maquillada; suelo 25 fps, objetivo 50 en triángulos pequeños. Medido en `088_blit3d_planes` (2 planos, cubo): 12.5 fps → **28.2 debug / 30.8 release** tras quitar módulos por variable del clip, tipar el pipeline, acotar el area fill al bbox y añadir el fast path de frustum (bucle de caras 205k → 58k ciclos/frame). El relleno Blitter es ahora el 54 % (135k): siguiente recorte (menos blits de arista). El relleno CPU (`fill_cpu`) queda descartado para objetos pequeños: 1.14 fps |
| **P6 — Robustez y regresión** | Fuzz host del clip con polígonos aleatorios (dentro/fuera/cruzando), secuencias de visión y flicker de las tres demos en el gate | Casos añadidos a HOST-481 + `VALIDATION.md` de cada demo | Suite por demo en verde (build/run/analyze/flicker), `flicker-baseline.json` declaradas |

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
- **Precisión 4.12**: objetos muy lejos o muy cerca saturan el rango; el contrato del renderer es «cámara lejos del near» y el clip evita la división inválida. Se documenta el rango útil.
- **Subpíxel**: sin él, aristas con escalones de 1 px; aceptado (F6 de presentación) y no bloquea el suelo de fps.

## Demos y numeración

- `demos/techniques/amiga/3d/087_blit3d_triangles`, `088_blit3d_pentagons`, `089_blit3d_solids` (siguiente libre del ámbito: 087; `NEW_NUMBERING` en [`NUMBERING.md`](../../ai-dev-environment/NUMBERING.md)).
- Test host nuevo en `tests/host/graphics/481_render3d_clip` (siguiente libre del ámbito).

## Referencias

- Arquitectura: [`3D_GAME_ARCHITECTURE.md`](../../engine/architecture/3D_GAME_ARCHITECTURE.md) (paquete 1), [`3D_RENDER_VS_PHYSICS.md`](../../engine/architecture/3D_RENDER_VS_PHYSICS.md), [`RASTER.md`](../../engine/architecture/RASTER.md).
- Técnicas: [`blitter-line-subpixel-fill.md`](../../reference/amiga/techniques/blitter-line-subpixel-fill.md), [`flat-shade` de la 116](../../../demos/techniques/amiga/effects/116_flatshade_convex/README.md) y la decisión por cara en [`FLATSHADE_CONVEX_PORT_PLAN.md`](../../demos/effects/FLATSHADE_CONVEX_PORT_PLAN.md).
- Estado vigente del raster: [`ROADMAP_UNIFICADO.md`](ROADMAP_UNIFICADO.md) §rasterizado; presentación: [`ROADMAP_3D_PRESENTACION.md`](ROADMAP_3D_PRESENTACION.md).
