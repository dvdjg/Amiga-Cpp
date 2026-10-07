# Sprites HW y BOBs para videojuegos: análisis de adecuación y roadmap

Este documento responde a una pregunta concreta: **¿el soporte de sprites hardware y BOBs del engine es adecuado para desarrollar videojuegos, o puede simplificarse?** Contrasta el inventario de técnicas históricas (la consulta externa sobre sprites, con Free Form, Risky Woods, Jim Power, multiplexado, compuestos y trayectorias) con las piezas que el engine ya tiene, separa lo que existe de lo que es propuesta y fija el **roadmap de implementación** de lo que falta con los algoritmos y clases extraídos.

La **lista viva y priorizada de pendientes de sprites** sigue en `ROADMAP_UNIFICADO.md` §«Sprites hardware — estado»; este documento añade la lectura **orientada a juego** (objetos compuestos, trayectorias, mecanismos, emisión) y las simplificaciones apropiadas. Los contratos de representación, transparencia, fondos y Copper por objeto no se repiten aquí: viven en `OBJECT_SYSTEM.md`; el reparto de canales por intervalo, en `SPRITE_CHANNEL_WINDOWS.md`; el inventario de técnicas y sus costes, en `sprite-techniques-catalog.md` y `sprite-tricks-games.md`.

```text
  ┌──────────────────────────────────────────────────────────────────────┐
  │ LO QUE YA EXISTE (bajo nivel)        │ LO QUE FALTA (capa de juego) │
  │ ─────────────────────────────        │ ──────────────────────────── │
  │ 8 canales + ocupación exacta         │ personajes/vehículos          │
  │ attached 15 colores                  │   COMPUESTOS multi-parte      │
  │ tiras, cadenas, ventanas             │ trayectorias y formaciones    │
  │ Risky Woods / Free Form / HUD línea  │   (shmup) + pool de entidades │
  │ animación DATA por frame             │ mecanismos de escenario       │
  │ degradación sprite → BOB             │   (cuerda/puente, muelle…)    │
  │ colisión CLXCON/CLXDAT               │ hitboxes por frame            │
  │ BOB: cookie-cut/OR/opaco/save-under  │ emisión DMA encadenada        │
  └──────────────────────────────────────────────────────────────────────┘
```

### Vocabulario (convención del proyecto)

- **Sprite** (a secas): la **entidad gráfica animada de alto nivel** de un juego —contenido, animaciones, ancla, hitboxes y comportamiento (el sprite del jugador, el de un camión, el de una bala)—. Puede materializarse con cualquier combinación de piezas internas. En el engine se describe con `ActorDesc` + `Animation` y, cuando es multi-parte, con `CompositeVisual`/`CompositeState` (F1).
- **Sprite HW**: uno de los 8 canales DMA de Agnus (`graphics/sprite_manager.hpp`). Se nombra siempre «Sprite HW», nunca «sprite» a secas, para no mezclar capas.
- **BOB** (*Blitter object*, «sprite blob»): copia de bitmap por Blitter (`graphics/bob.hpp`).
- **Sprite CPU**: objeto rasterizado por la CPU (en A1200, *CPU Blit Assist*); corresponde a `Representation::Cpu`, aún sin camino de juego implementado.
- **Sprite Playfield**: una capa de playfield (p. ej. PF2 de un DPF) usada como un objeto grande con scroll (enemigo gigante, patrón Jim Power); corresponde a `Representation::Layer`.
- **Materialización**: el cómo se dibuja este frame un sprite (Sprite HW / BOB / CPU / Playfield). La elige el **planner** (`RepresentationAllocator` y composición) y puede degradar sin cambiar el contenido.
- **Contenido**: los píxeles, máscara y frames (`Visual`), sin flags de hardware ni de representación.

**Frontera de capas (regla):** el contenido dice **qué se ve**; la preferencia de materialización vive en `ActorDesc::preferred` (`Representation`); la decisión efectiva (canal, par *attached*, layout, paleta, Copper) la toma el **planner/materializador** a partir de la forma del contenido y del presupuesto. Ni `Visual` ni `CompositePart` llevan registros, canales ni flags de Sprite HW.

## 1. Veredicto

1. El soporte de **bajo nivel** no es el cuello de botella: cubre ya todo el inventario clásico relevante para un juego —multiplexado vertical con ocupación exacta por línea, pares *attached* de 15 colores, tiras horizontales, cadenas «chasing the raster», ventanas de canal para fondos, colisión `CLXCON`/`CLXDAT`, prioridad `BPLCON2`, animación de DATA por frame y degradación sprite→BOB—. La deuda **no** es «faltan técnicas de chipset»: es la capa de juego que las usa.
2. Lo que falta para hacer juegos con este soporte es una **capa de contenido y dinámica**: personajes/vehículos **compuestos** (varias partes, secuencias por parte, anclaje, hitboxes), **trayectorias y formaciones** de shmup con pool de entidades, y **mecanismos de escenario** (cuerdas/puentes, muelles, catapultas). Es lo que convierte «puedo pintar 8 sprites y N BOBs» en «puedo hacer un juego».
3. **Sí puede simplificarse**, pero por consolidación, no por recorte de capacidad: hay **cuatro rutas de emisión** en `SpriteManager` (tres de ellas legado o de driver), dos modelos de animación (`Anim`/`Animation`) y el estado de sprites replicado en cuatro documentos. Las simplificaciones concretas (campos muertos, comentarios obsoletos y camino canónico) están en §4.
4. La mejora de mayor valor no es tocar el chipset: es **materializar el inventario de la consulta como capa de juego** (§5, fases F1–F6). La emisión DMA encadenada (F3) y el *pacing* del fondo Jim Power son los únicos puntos donde el engine puede ganar coste por frame, y ambos exigen verificación contra la fuente del emulador antes de afirmar nada (`AGENTS.md` §0 y §1.12).

## 2. Inventario del soporte actual

### 2.1 Sprites hardware

| Capacidad | Pieza | Dónde | Verificación |
|---|---|---|---|
| Reparto de canales: first-fit + **ocupación exacta por línea** (bitfield 256 bits/canal, sin heap) | `SpriteAllocator` | `graphics/sprite_allocator.hpp` | HOST-003 |
| Canal preferido (`channel`) y prioridad de asignación (`assign_rank`, pasadas 3→0) | `SpriteIntent` | `graphics/raster_intent.hpp` + allocator | HOST-003/072 |
| **Grupos con trayectoria**: corrida contigua de canales para el bounding box del grupo, o entero a BOB | `group_id`/`group_index`/`group_span` | `graphics/sprite_allocator.hpp` | HOST-003 |
| **Tiras horizontales** (objeto >16 px): corrida contigua, o entera a BOB | `strip_id`/`strip_index`/`strip_span` | allocator | HOST-003 |
| **Cadenas verticales** («chasing the raster»): mismo canal para todas las franjas, rearme por franja | `chain_id`/`chain_index`/`chain_span` | allocator + `sprite_template_to_intents` | HOST-003/428; demo 217 |
| **Pares *attached*** (15 colores): cocinado de las dos estructuras DMA (cabecera + terminador) | `cook_attached_pair` | `graphics/sprite_attached.hpp` | HOST-427/428; demos 214/216 |
| **Ventanas canal × intervalo** para fondos + objetos, con `occupy_run`/`free`/`free_run` | `SpriteChannelWindow`, `SpriteChannelLedger`, `plan_sprite_windows` | `graphics/sprite_channel_window.hpp` | HOST-416 |
| Fondo repetitivo **Risky Woods** (reposiciona `POS` cada ≥24 px) | `effects::RiskyWoodsLayer` | `api/effects.hpp` | HOST-417; demos 208/211 |
| Capa/HUD por línea (parchea `POS`+`DATA` por scanline) | `SpriteLineLayer` | `graphics/sprite_line_layer.hpp` | HOST-418 |
| Fondo **Free Form** no repetitivo (fachada con scroll por Copper) | `effects::FreeFormSpriteLayer` | `api/effects/free_form_sprite.hpp` | HOST-419; demo 212 rota, mejora apuntada en 213 |
| Emisión: armado de objeto (`arm_object`), armado por placements con **rearme vertical** | `SpriteManager` | `graphics/sprite_manager.hpp` | HOST-428; demos 053/054/214/216/217 |
| Colisión de hardware (`CLXCON`/`CLXDAT`), sin posición | `SpriteCollisionConfig`/`decode_clxdat` | `graphics/sprite_collision.hpp` | demo 206 |
| Prioridad sprite/playfield (`BPLCON2`), `Priority` intent | `emit_planes_display` + `CopperIntentKind::Priority` | `graphics/copper/scheduler.hpp`, `raster_intent.hpp` | 206/216; `winuae/sprite-color-priority.md` |
| Animación de DATA por frame (hoja + `frame_stride`) de un objeto HW | `compose_sprites` publica el frame vigente | `scene/actor_sprite.hpp` | HOST-072; demos 054/216 |
| Límites de hardware (canales, gap, reuso ≥24 px, planos de parcheo) | `kSpriteChannels`, `kSpriteVerticalGapLines`, `kSpriteMinReusePx`, `kSpriteLineDataMaxBitplanes` | `graphics/sprite_limits.hpp` | fuente única citada |
| Fachada de juego (actores → compuestos HW + fallback BOB + Copper) | `SpriteScene<MaxActors>` | `api/sprites.hpp` | HOST-391/072; demos 216/217 |

### 2.2 BOBs (objetos de Blitter)

| Capacidad | Pieza | Dónde | Verificación |
|---|---|---|---|
| Dibujo de BOB por copia/máscara con barrel shifter: **OR, opaco, AND y cookie-cut** | `Bob`, `BobDraw`, `bob_draw` | `graphics/bob.hpp` | HOST-072; demo 086 |
| Cookie-cut interleaved en **un solo blit** `$CA` (máscara empaquetada por pares) | `BobMaskPack::InterleavedPair` | `bob.hpp` + `blit_job.hpp::make_interleaved_masked_bob` | `interleaved-bob-single-blit.md`; demo 213 |
| Borrado por caja, **save-under** (guardar/restaurar) y rectángulos previos | `bob_erase_box`, `bob_save_box`, `bob_restore_box` | `graphics/bob.hpp` | HOST-072; demo 086 |
| Plan de blits con presupuesto, dirty rects, orden y kinds (`CopyRect`, `ClearRect`, `RestoreRect`, `C2P`, `TileBlockCopy`…) | `FramePlan`, `BlitJob` | `graphics/frame_plan.hpp` | HOST-072/368 |
| Capa ligera de BOBs con hoja homogénea/heterogénea, `z` y animación | `BobLayer` | `scene/bobs.hpp` | demos 213 |
| **Dual Playfield Fast BOBs**: copia con padding (dibuja y limpia en un blit) con degradación a cookie-cut + clear | `FastBobLayer` (`BobDraw::Opaque` = copia `$F0`) | `scene/bobs.hpp` | HOST-355; `dual-playfield-fastbobs.md` |
| Lote de BOBs OR intercalados sin `jsr` por objeto | `OrBlobBatch` | `platform/amiga/blob.hpp` | HOST-176 |
| Políticas de transparencia y fondo por actor (`ColorKey0`, `Mask1Bit`, `AdditiveOr`, `SaveUnder`, `Auto`…) | `TransparencyMode`, `BackgroundPolicy`, `resolve_background` | `scene/actor_types.hpp` | HOST-072 |
| Tiles como BOB (banco común + posición de mapa) | `BlitJobKind::TileBlockCopy` | `frame_plan.hpp`, `field/xlimited.hpp` | demos 100–112 |

### 2.3 Límites de alcance declarados

El sistema **no** cubre hoy (y está bien que no lo haga en el núcleo): recorte parcial de un objeto que no cabe entero en la ventana (se rechaza, documentado en `OBJECT_SYSTEM.md` §2), anclaje distinto por frame, objeto CPU con política de fondo, y capa de hitboxes por frame (no existe un tipo de «caja de golpe» que siga a la animación). Los tres primeros son estados del sistema de actores, no del chipset; el cuarto es parte de F1 de este roadmap.

## 3. Contraste con el inventario de técnicas (consulta externa)

La tabla mapea cada técnica de la consulta al estado real del engine. «Cubierta» significa que el contrato existe y está verificado por test host y/o demo; «parcial» o «propuesta» remite a la fase del roadmap §5.

| Técnica del inventario | Estado en el engine | Pieza / fase |
|---|---|---|
| 1. Sprites como objetos de juego (jugador, enemigos, balas, HUD) | Cubierta | `SpriteScene`, `compose_sprites`, demo 216/217 |
| 2. Attached (15 colores) | Cubierta | `cook_attached_pair`, allocator de pares; demos 214/216 |
| 3. Multiplexado vertical (reuso del mismo canal en líneas distintas) | Cubierta | `SpriteAllocator` (first-fit + bitfield) y `emit_placements_into` |
| 4. Multiplexado horizontal con Copper (`SPRxPOS` a media línea) | Cubierta para HUD/línea y Risky Woods; Free Form por ventana, propuesta | `SpriteLineLayer`, `RiskyWoodsLayer`; F4 (driver `FreeForm`) |
| 5. Fondo repetitivo Risky Woods | Cubierta | `effects::RiskyWoodsLayer` (HOST-417) |
| 6. Free Form Sprite Layer (sin patrón repetitivo) | Parcial: fachada + test; demo 212 rota | `effects::FreeFormSpriteLayer` (HOST-419); F4 |
| 7. Combinación sprites + BOBs (sprites = jugador/HUD/fondo; BOBs = resto) | Cubierta | Degradación `as_bob` + `emit_bob_fallbacks`; `FastBobLayer` en DPF |
| 8. Detección de colisiones (hardware y software) | Hardware cubierta (sin posición); software: primitivas, sin hitboxes por frame | `sprite_collision.hpp`; `core/util/collision.hpp`; F1 |
| 9. Efectos de Copper sobre offsets/posiciones por fila (bending) | Propuesta | F4 (driver de bending con tabla de seno reutilizada de `core/math/sinetable.hpp`) |
| 10. Cambios de color por Copper (palette splitting por franjas) | Parcial: `HwSpritePaletteSwitch` de plantilla y zonas de paleta por frame | F4 (demo y generalización) + F7 (requisitos y arbitraje por canal/franja/escena) |
| 11. Modos avanzados: sprites manuales, chasing extremo, parallax, HUD por sprites | Cubierta en su mayoría | `SpriteHorizontalRearm`, `chain_*`, `SpriteLineLayer` |
| Algoritmo de reparto: first-fit + `lastY[8]` / **bitfield por línea** | **Cubierta y superada**: el bitfield es la versión exacta con arrays fijos (sin `std::bitset`) | `sprite_allocator.hpp` (HOST-003) |
| Grupos de trayectoria (ristras/formaciones) reservando canales | Cubierta a nivel de asignación | `group_*` (HOST-003); la generación de formaciones es F2 |
| Ristra vertical de tiros = 1 canal | Cubierta: el allocator reutiliza el canal; el armado rearma por franja | `emit_placements_into` (HOST-428) |
| **DMA encadenado por canal** (sin rearme de Copper) | Propuesta | F3 |
| Selector automático de estrategia de emisión (PureDMA/híbrido/Copper) | Propuesta (hoy hay un solo camino de emisión de objetos) | F3 |
| **Compuestos** de personaje (partes, secuencias, articulación, ancla, hitboxes) | Propuesta | F1 |
| Trayectorias paramétricas, formaciones con delay, pool de proyectiles | Propuesta | F2 |
| FastCopy (copia con padding) | Cubierta como política (`BobDraw::Opaque` + `FastBobLayer`) | Márgenes automáticos en el asset: F5 |
| Cuerdas/puentes flexibles, muelles, catapultas, puertas, destructibles | Puertas/destructibles/sólidos: diseño en `PIXEL_ART_2D_ISOMETRIC.md` §7; cuerdas/muelles/catapulta: propuesta | F6 |

## 4. Simplificación

### 4.1 Camino canónico de emisión (documentado; retirada del legado, propuesta)

`SpriteManager` acumuló cuatro rutas de emisión a lo largo del desarrollo de las demos. El análisis recomienda declarar **una canónica** y tratar el resto como drivers locales:

| Ruta | Para qué sirve | Estatus propuesto |
|---|---|---|
| `emit_placements_into` | **Camino de juego**: placements del compositor, primera config por canal en línea temprana, rearme vertical y paleta por franja intercalada | **Canónico** |
| `emit_armed_into` + `arm_object` | Armado directo de los 8 canales de un `SpriteManager` configurado a mano (demos 054, HOST-428) | Driver de bajo nivel (demos) |
| `emit_template_into` | Plantilla de franjas sobre UN canal con `WAIT` por segmento (demo 053) | Driver de bajo nivel (demos) |
| `emit_into` | Un `WAIT` por `VSTART`; usado por demos antiguas (062, dk_port) | **Legado**: retirar cuando esas demos migren a `emit_armed_into`/`emit_placements_into` |

La retirada de `emit_into` no se aplica todavía (rompería demos antiguas aún no migradas); lo que sí se ha aplicado es **dejar el contrato escrito** en la cabecera para que la migración sea mecánica y nadie añada rutas nuevas al mismo tiempo. Regla para código nuevo: la lógica de juego **no llama a `SpriteManager` directamente** — usa `SpriteScene::emit` + `emit_placements_into` (o `Screen`/API de nivel A cuando exista el equivalente).

### 4.2 Campos muertos (aplicado)

`SpriteConfig::palette_base` y `Visual::palette_base` se escribían pero **nunca se leían** en la emisión. Además su semántica documentada era engañosa: los sprites **no** tienen una «base de paleta» por sprite; usan siempre `COLOR16..31` y los canales de un par **comparten** sus colores (AHRM cap. 4 «Attached Sprites»; `sprite-layer.md` §3). Se han eliminado ambos campos y las asignaciones de las demos que los usaban; quien necesite color por objeto lo hace con `CopperIntent`/`HwSpritePaletteSwitch`, que es el mecanismo real.

### 4.3 Comentarios y estados obsoletos (aplicado)

- `BobErase::RestoreUnder` decía «PENDIENTE» en `bob.hpp` mientras el save-under existe y se usa (`bob_save_box`/`bob_restore_box`, `scene::Actor` con `SaveUnder`, demo 086): el comentario pasa a describir el reparto real (`bob_erase` solo hace `ClearRect`; el save-under es una secuencia de jobs en la capa de actor/intención).
- El estado de sprites vive hoy en **cuatro** sitios (`OBJECT_SYSTEM.md` §2, `SPRITE_CHANNEL_WINDOWS.md` §9, `sprite-techniques-catalog.md` «Encaje en el engine» y `ROADMAP_UNIFICADO.md`). Propuesta de higiene (AGENTS §1.2/§1.3): las fichas de referencia describen **contrato**, no fases; la **única lista viva** de pendientes es `ROADMAP_UNIFICADO.md` §«Sprites hardware — estado», y este documento es el roadmap de la capa de juego. No duplicar tablas de estado en los cuatro.

### 4.4 Dos modelos de animación (`Anim` vs `Animation`): propuesta

`graphics/anim.hpp` (`Anim`) es una animación ligera de frames + duraciones para capas de BOB; `graphics/animation.hpp` (`Animation`, con `Frame {x,y,w,h,ticks,event}` y estado `elapsed`) es la animación de contenido de actor, con eventos y velocidad entera. El solape es real pero pequeño: `Anim` resuelve el caso de `BobLayer` sin arrastrar `Frame`/eventos. Recomendación: **no fusionar todavía**; documentar la frontera («capa ligera» vs «contenido de actor») y, si aparece un tercer uso, unificar sobre `Animation`. Cualquier compuesto (F1) usa `Animation`.

### 4.5 Descontaminación de la capa de contenido (`Visual`): aplicada y propuesta

- **Aplicado — `Visual::attached` eliminado.** Era un flag de Sprite HW dentro del descriptor de contenido: no significa nada si el contenido se materializa como BOB. Un Sprite HW de 16 px solo lee 1 word de datos por línea (2 planos), así que un contenido de **4 planos y `w <= 16`** solo cabe como **par *attached***: la condición se **deriva** (`visual_is_attached_pair`) y la materialización (cocinar las dos estructuras DMA, asignar la pareja de canales) es cosa del compositor. El mismo contenido sigue sirviendo de BOB sin flag alguno.
- **Aplicado — `VisualKind` retirado.** `VisualKind::HardwareSprite`/`Bob` duplicaba `ActorDesc::preferred` (`Representation`), que es quien elige el planner; y `Tile`/`FillRect` no son tipos de contenido (un tile es un BOB con origen en un banco de tiles y una rejilla; un rect es una primitiva de dibujo): son **semántica de juego** y no se usaban en ningún camino de materialización. `Visual` queda con `pixels`/`mask`/`w`/`h`/`bitplanes`/`frame_count`/`frame_stride`/`offset_x`; la preferencia va en `Representation`; «puede ser Sprite HW» se deriva del contenido (ancho, alto, planos).
- **Regla de frontera para nuevas distinciones**: si una distinción cambia **qué píxeles** se ven (frames, planos, máscara) es contenido; si cambia **cómo** se dibujan (canal, par *attached*, layout, paleta) es materialización y va al planner; si describe **qué significa** el objeto en el juego (tile, proyectil, plataforma) va a la capa de gameplay/entidades, no al descriptor gráfico.

## 5. Roadmap de implementación

Orden por **dependencia y valor para juego**. Cada fase declara objetivo, contrato extraído (adaptado al estilo del engine: sin heap, sin RTTI, sin STL, tipos `u8`/`s16`/`Span`/`Ref`), encaje y verificación.

| Fase | Estado |
|---|---|
| F1 actores compuestos | **Hecha**: `graphics/composite_visual.hpp` + `scene/composite_actor.hpp` (HOST-429) y fachada `api/objects.hpp` (`CompositeScene`, HOST-432); demo **063_composite_actors** validada con visión. `Visual.Kind` retirado y par *attached* derivado. |
| F2 trayectorias/formaciones/pool | **Hecha**: `scene/{trajectory,formation,entity_pool}.hpp` (HOST-430); demo **064_shmup_wave** (formación seno en espejo + pool de enemigos) validada con visión. |
| F3 DMA encadenado + selector | Propuesta (requiere verificación en fuente del emulador). |
| F4 cierre de pendientes de sprites | Drivers/demos pendientes (lista viva en `ROADMAP_UNIFICADO.md`). |
| F5 márgenes FastCopy | Base hecha (`FastBobLayer`, HOST-355); preproceso de márgenes pendiente. |
| F6 mecanismos de escenario | Propuesta (sólidos/puertas con diseño en `PIXEL_ART_2D_ISOMETRIC.md` §7). |
| F7 paleta de sprites | **Planificador hecho**: `graphics/sprite_palette.hpp` (HOST-433: conflicto → degrada por `z`, conmutación vertical por `PaletteLine`). Integración con allocator/fachada y demo pendientes. |
| F8 secuenciador (timeline) | **Hecho**: `core/util/sequence.hpp` (HOST-431); demo **064_shmup_wave** dirigida por timeline (ráfagas) validada con visión. |

**Hallazgo abierto asociado**: el alta dinámica de actores de sprite desde `update` (no desde
`init`) produce placements correctos pero no se publica en target; evidencia, descartes y plan en
`docs/debugging/investigaciones/064-sprite-hw-creado-en-update-no-publica.md`. La demo 064 lo
esquiva con un pool fijo reciclado por posición. Además, `bob_draw`/`bob_erase_box`/
`bob_save_box`/`bob_restore_box` rechazan ahora cajas con `y < 0` (antes envolvían el offset y
escribían fuera del bitmap; caso visto en la 064).

### F1. Actores compuestos (personajes, vehículos, jefes)

**Objetivo:** describir un objeto como **varias partes** (piernas, torso, arma, rueda, aura) con animación **por parte** y secuencias que cambian por condiciones de juego, con ancla y **cajas de golpe** por frame. Es el ladrillo que más acerca el engine a un juego (el ejemplo clásico: piernas por un lado y resto del cuerpo por otro).

**Contrato (contenido inmutable, cocinable en host):**

```cpp
// eng/graphics/composite_visual.hpp (propuesto)
struct HitBox { Box box {}; u8 group = 0; bool solid = true; };

struct CompositePart {
    Visual visual {};        // contenido puro: sin flags de representación (§4.5)
    s16    offset_x = 0;     // respecto al ancla del actor
    s16    offset_y = 0;
    u8     z = 0;            // orden de dibujo dentro del compuesto
};

struct CompositeFrame {
    Span<const u8>     part_frames {}; // índice de frame de cada parte
    Span<const HitBox> hits {};        // cajas del frame (vacío = hereda bounds)
    u16 ticks = 1;                     // duración en ticks de juego
    u16 event = 0;                     // evento opcional (sonido, golpe, fin)
};

struct CompositeSequence {
    Span<const CompositeFrame> frames {};
    bool loop = true;
    u8   next = 0xff;        // 0xff = se queda; si no, secuencia al terminar
};

struct CompositeVisual {
    Span<const CompositePart>     parts {};
    Span<const CompositeSequence> sequences {};
    Anchor anchor {};                // pies, centro… (reutiliza scene::Anchor)
    Box    bounds {};                // hitbox por defecto si el frame no trae
};
```

**Estado de runtime (enteros, sin heap):**

```cpp
// eng/scene/composite_actor.hpp (propuesto)
struct CompositeState {
    u8  sequence = 0;
    u16 frame = 0;
    u16 elapsed = 0;
    bool finished = false;
    bool facing_left = false;
    s16 x = 0, y = 0;                    // posición del ancla
};

bool composite_advance(const CompositeVisual&, CompositeState&, u16 ticks);  // O(1) amortiguado
void composite_set_sequence(CompositeState&, u8 seq, bool restart = true);
u8   composite_hitboxes(const CompositeVisual&, const CompositeState&, Span<Box> out);
```

**Materialización (tipos concretos, sin virtual en el camino caliente):** dos funciones libres que leen el mismo estado; la materialización de cada parte (Sprite HW si su forma lo permite y hay canal, si no BOB) la decide el planner, no un flag en el contenido (§4.5):

```cpp
// HW: una SpriteIntent por parte HardwareSprite activa (el allocator reparte después).
u8 composite_to_sprite_intents(const CompositeVisual&, const CompositeState&,
                               Span<SpriteIntent> out, u8 first_channel = 0u);
// BOB: dibuja las partes Bob del frame en el FramePlan (borrado según política).
u8 composite_emit_bobs(const CompositeVisual&, const CompositeState&,
                       FramePlan&, const BobTarget&);
```

**Preprocesado (host o carga de nivel):** partes ordenadas por `z`; offsets espejados precalculados (`offset_x_left = -offset_x - w`) para que el facing no compute nada; hojas por parte ya en formato final (interleaved BOB / DAT-DATB de sprite); validación de `part_frames[i]` en rango; hitboxes por frame ya en coordenadas del ancla.

**Encaje:** una extensión del sistema de actores (`ActorDesc` gana un `eng::Ref<const CompositeVisual>` opcional y `Actor` lleva `CompositeState`), o un `CompositeScene` paralelo que reusa `SpriteScene` por cada parte. La segunda opción no toca `ActorStore` y permite mezclar ya mismo; la primera es la integración final. El espejo y el `z` se resuelven al cocinar; en runtime solo sumas.

**Verificación:** HOST (avance con secuencias/eventos, hitboxes con espejo, partes fuera de orden) + demo de personaje multi-parte con body BOB + arma sprite HW.

**Coste runtime:** O(partes activas) sumas y una lectura por parte; hitboxes O(cajas del frame).

### F2. Trayectorias, formaciones y pool de entidades (shmup)

**Objetivo:** olas de naves/tiros con trayectorias predefinidas, delays y formaciones (V, abanico, columnas), y un pool sin heap para proyectiles/enemigos ligeros. Es el patrón de juego que más partido saca del **reuso vertical de sprites** (una ristra de tiros verticales = 1 canal, ya soportado por el allocator).

**Contrato:**

```cpp
// eng/scene/trajectory.hpp (propuesto)
struct PathPoint { s16 x = 0, y = 0; u16 ticks = 1; };   // relativo al origen si `relative`

struct Trajectory {
    Span<const PathPoint> points {};
    bool loop = false;
    bool relative = true;                                     // offsets respecto al spawn
};

struct TrajectoryFollower {
    u16 point_index = 0;
    u16 elapsed = 0;
    s16 origin_x = 0, origin_y = 0;
    bool finished = false;
};

void trajectory_advance(const Trajectory&, TrajectoryFollower&, u16 ticks, s16& out_x, s16& out_y);
```

**Generadores** (setup/constexpr; la tabla de seno **ya existe**, no se añade otra): `eng/core/math/sinetable.hpp` (`SineTable<Amp, Steps, T, Offset>`, generada en compilación, con la muestra en el tipo que se pida) y `eng/retro/sintab.hpp` (la 4.12 exacta, `kSinTab`); el coseno es la misma tabla con la fase desplazada un cuarto de período.

```cpp
u16 gen_line(Span<PathPoint> out, s16 dx, s16 dy, u16 length, u16 ticks_per = 1);
u16 gen_sine_vertical(Span<PathPoint> out, s16 amplitude, s16 y_step, u16 length,
                      u8 phase0 = 0, u16 ticks_per = 1);
```

**Rutas con spline (posición, velocidad y orientación).** Las curvas **ya existen** en el engine: `eng::math::bezier2`/`bezier3`/`hermite`/`catmull_rom` (`core/math/spline.hpp`, genéricas sobre el escalar `S` y `Vec<N,S>`) más `lerp`/easings (`core/math/interp.hpp`). Una ruta spline es una **vista de puntos de control** (no propietaria) que el seguidor evalúa en runtime; sirve para formaciones que dibujan arcos suaves, jefes y proyectiles:

```cpp
// Propuesta: el contenido de la ruta son los puntos de control (vivos, sin copia).
template <typename S>
struct RouteSpline {
    Span<const Vec<2, S>> controls {};  // p0..pn; Bézier cúbica (grupos de 4) o Catmull-Rom
    bool closed = false;
};
// Muestrea posición y orientación (derivada) en `t`.
template <typename S>
void route_sample(const RouteSpline<S>&, S t, Vec<2, S>& out_pos, s16& out_angle256);
```

- **Velocidad por longitud de arco**: para que `t` avance en píxeles (y no en parámetro), el setup precalcula una tabla de longitudes por tramo (con `eng::math::isqrt` cuando haga falta); el seguidor avanza por ella y la velocidad es constante sin `sqrt` por frame.
- **Orientación**: derivada numérica (`sample(t)` vs `sample(t+Δ)`), lista para naves y proyectiles.
- **Puntos de control animables**: `controls` es un `Span` vivo; el secuenciador (F8) o la lógica del juego pueden moverlos y la ruta cambia en marcha sin recocinar el asset.
- `Trajectory`/`TrajectoryFollower` valen igual para polilínea y spline: la ruta expone `sample`/longitud; la tabla `PathPoint` es la versión **cocida** (constexpr/assets) de la ruta.

**Formaciones y spawner:**

```cpp
struct FormationMember { u16 delay_ticks = 0; s16 offset_x = 0, offset_y = 0; u8 trajectory = 0; };
struct Formation { Span<const FormationMember> members {}; s16 spawn_x = 0, spawn_y = 0; };
struct FormationState { u16 age = 0; bool active = false; };

template <class SpawnFn>   // spawn(idx, trajectory_id, world_x, world_y)
void formation_update(const Formation&, FormationState&, u16 ticks, SpawnFn spawn);
```

**Pool de entidades** (capacidad de plantilla, búsqueda de hueco O(n) con n pequeño; si hace falta, lista de libres):

```cpp
enum class EntityKind : u8 { None = 0, Projectile, Enemy, Debris };

template <u16 MaxEntities>
struct EntityPool {
    struct Slot {
        EntityKind kind = EntityKind::None;
        TrajectoryFollower traj {};
        u8  trajectory_id = 0;
        const CompositeVisual* visual = nullptr;   // o Visual simple
        CompositeState anim {};
        s16 x = 0, y = 0, prev_x = 0, prev_y = 0; // prev para borrado BOB
        u8  hp = 1, damage = 1, faction = 0;
        bool active = false, vertical_stream = false; // candidata a grupo de 1 canal
    } slots[MaxEntities] {};

    u16 spawn(EntityKind, const CompositeVisual*, const Trajectory*, u8 traj_id,
              s16 x, s16 y, u8 sequence = 0, bool vertical = false);
    void kill(u16 index);
    void update(Span<const Trajectory> level_trajectories, u16 ticks);
};
```

**Encaje con el reparto de canales ya existente:** las entidades con `vertical_stream` se recolectan en una lista y se les asigna el **mismo grupo** (`SpriteIntent::group_id`, ya soportado); si el grupo no cabe en un canal, degrada entero a BOB de forma coherente (comportamiento actual del allocator). No hace falta código nuevo en el allocator.

**Verificación:** HOST (avance de trayectoria con ticks múltiples y `loop`, spawner con delays, pool lleno/vacío, culling) + demo de ola en V con tiros verticales.

### F3. Emisión DMA encadenada y selector de estrategia

**Objetivo:** cuando un canal sirve **varios objetos en líneas disjuntas** (multiplexado vertical, ristras), hoy el emisor rearma el canal con Copper (`WAIT` + `POS/CTL/PT` por placement). La alternativa «pura DMA» construye **una sola estructura encadenada** por canal (`[…][POS,CTL,DATA…][POS,CTL,DATA…]…0,0`) que Agnus recorre sola: cero MOVEs de rearme. Es el «DMA encadenado por canal» pendiente de `sprite-multiplexer-bob-fallback.md` §9.

**Contrato:**

```cpp
// Buffer del llamador en Chip (lo lee el DMA) + cursor.
struct SpriteDmaChain {
    ChipView<SpriteTag> storage {};
    u16 used = 0;                        // words escritas
};

// Escribe los placements del MISMO canal como cadena POS/CTL+DATA, con el gap de
// kSpriteVerticalGapLines ya garantizado por el allocator y terminador 0,0.
bool build_sprite_dma_chain(Span<const HwSpritePlacement> channel_placements, SpriteDmaChain& out);
```

**Selector** (el compositor elige por canal; criterios objetivos, no heurística difusa):

| Condición | Estrategia |
|---|---|
| Ristra vertical, imágenes iguales o copiables | **PureDMA** (cadena) |
| Mismo canal con pocas apariciones o imágenes grandes que no conviene duplicar | **Rearm por Copper** (camino actual, `emit_placements_into`) |
| ≥3-4 reapariciones y datos pequeños | **PureDMA** |
| Caso por defecto / seguridad | Rearm por Copper |

**Riesgo y verificación obligatoria:** el formato del encadenado y el comportamiento del DMA al saltar de estructura deben contrastarse con la fuente del emulador (`../WinUAE-DBG/`, ficha `winuae/sprite-dma.md`) y el AHRM cap. 4 **antes** de activarlo; un encadenado mal formado produce columnas fantasma. La verificación mínima: test host que compare la **secuencia de palabras** generada contra la del camino de rearme (misma semántica POS/CTL/PT) y demo A/B de una ristra de tiros (DMA vs Copper) con Ollama y fps. Sin esa evidencia, la fase no se da por buena (`AGENTS.md` §0/§1.12).

**Encaje:** `SpriteManager` (nuevo método de armado de cadena) + `compose_sprites`/`SpriteScene` (selector). No cambia el contrato de `HwSpritePlacement`.

### F4. Cierre de pendientes de sprites (drivers y demos)

Los pendientes listados en `ROADMAP_UNIFICADO.md` se mantienen allí como fuente única; aquí se ordenan por valor de juego y se añade lo que aporta la consulta:

1. **Driver `FreeForm` por ventana** (`SpriteChannelLedger` + `effects::FreeFormSpriteLayer`): hoy hay fachada y HOST-419, pero la demo 212 está rota y el driver por ventana sigue propuesto en `SPRITE_CHANNEL_WINDOWS.md` §9. Valor: fondos de 3-4 colores libres sin gastar bitplanes.
2. **Bending por tabla de seno** (`SPRxPOS` por línea): driver que emita un `SpriteHorizontalRearm` por línea con `x = base + sine[phase] & 255` (reutilizar `core/math/sinetable.hpp`). Valor: agua, ondas, deformaciones.
3. **Palette splitting de sprites por franjas**: generalizar `HwSpritePaletteSwitch` fuera de la plantilla (intención `PaletteLine` sobre `COLOR16..31`) + demo. Valor: mismo sprite con colores distintos arriba/abajo.
4. **Jim Power DATA-por-línea (demo 215)**: pacing pendiente de medición y de la consulta a Grok; el fallback `POS+DATB+DATA` por columna (Free Form) ya está validado en 213.
5. **Demos de cierre**: HUD con `SpriteLineLayer` (HOST-418 sin demo), prioridad `BPLCON2` por franjas (`Priority` sin demo), y decidir 087 (arreglar o retirar; la técnica ya la cubren 207/208), y **orden incremental por Y** (reutilizar el orden del frame anterior en `build_sprite_intents`).

### F5. FastCopy: márgenes automáticos y preproceso

El camino rápido de dual playfield ya existe (`BobDraw::Opaque` = copia `$F0` + `FastBobLayer` con padding). Lo que falta es que **el padding no lo gestione el juego a mano**: el asset (parte de compuesto o `Sprite`) declara la **velocidad máxima por eje** y el pipeline host calcula los márgenes (`left ≥ máx. desplazamiento a la derecha`, `right ≥ máx. a la izquierda`, doblados si hay doble buffer) y el tamaño final del bitmap. La capa sigue decidiendo copia vs degradación (movimiento mayor que el padding o solape). Verificación: HOST del cálculo de márgenes + `FastBobLayer` ya cubierto por HOST-355; demo DPF con BOBs rápidos.

### F6. Mecanismos de escenario (cuerdas/puentes, muelles, catapultas)

Los sólidos, puertas y destructibles **ya tienen diseño objetivo** en `PIXEL_ART_2D_ISOMETRIC.md` §7 y primitivas en `eng/core/util/collision.hpp`; no se duplican. Lo que aporta la consulta y no existe es la **dinámica barata de elementos flexibles y articulados**:

```cpp
// eng/scene/rope.hpp (propuesto): cuerda/puente por eslabones (BOBs o sprites).
struct RopePoint { s16 x = 0, y = 0, old_x = 0, old_y = 0; bool pinned = false; };

struct Rope {
    Span<RopePoint> points {};
    s16 rest_length = 8;     // distancia entre puntos consecutivos
    s16 gravity = 1;         // px/tick²
    u8  iterations = 2;      // pasadas de restricción (1-3 basta)
    u8  damping = 240;       // 0..256 (256 = sin fricción)
};

void rope_step(Rope&);                       // verlet + restricciones de distancia
void rope_apply_impulse(Rope&, u8 index, s16 ix, s16 iy);
// Emisión: un eslabón (CompositeVisual de 1 parte o BobLayer) por punto/segmento.
```

Decisiones de coste: la restricción de distancia puede hacerse con la aproximación sin `sqrt` (`diff * dx / (4·dist²)`) o con `eng/core/math/isqrt.hpp` si la estabilidad lo pide; el puente se puede **congelar** cuando no hay peso encima y simular solo al pisarlo. Muelles y catapulta son máquinas de estado cortas (compresión + impulso; ángulo + `sine_scale` para el tip); la catapulta articulada se beneficia de F1 (brazo multi-parte con secuencias por rango de ángulo). Verificación: HOST de la simulación (conservación aproximada, extremos, impulso desde arriba) + demo de puente con jugador cruzando.

### F7. Paleta de colores de los sprites: requisitos y arbitraje

**Problema.** La paleta de los Sprites HW es el espacio `COLOR16-31`, y ese espacio se comparte en tres ejes que hoy no están coordinados por el planner:

1. **Entre los dos canales de un par** (par e impar comparten sus colores: *Color Bleed*, `sprite-layer.md` §3). El engine lo documenta y lo esquiva a mano (`preferred_channel` + `assign_rank` fijan la plantilla a su par), pero el `SpriteAllocator` no recibe requisitos de paleta y puede mover un objeto a un par con otros colores.
2. **Entre sprites de la misma franja horizontal** que comparten canal a distintas líneas: la paleta del canal es una sola por línea, así que dos reusos verticales con paletas distintas exigen conmutar `COLORxx` por Copper en la línea del segundo (lo que ya modela `CopperIntentKind::PaletteLine`).
3. **Con la escena.** Con 4 planos el playfield usa `COLOR00-15` y los sprites `COLOR16-31` (disjuntos); con **5+ planos / 32 colores, EHB o HAM** el playfield y los Sprites HW comparten el mismo fichero de registros, de modo que un cambio por franja afecta a los dos. Las restricciones de cada modo están documentadas en `sprite-layer.md`/`sprite-color-priority.md`.

**Diseño propuesto.** Declarar la **necesidad de paleta** como un requisito de materialización (no del contenido):

```cpp
// Propuesta: por sprite, las entradas COLORxx que necesita y en qué franjas.
struct SpritePaletteNeed {
    u8  first = 16;      // primera entrada (16..31)
    u8  count = 0;       // entradas usadas
    u16 top = 0, bottom = 0; // franja en la que aplica (0,0 = todo el frame)
};
```

- El compositor agrega las necesidades por **canal × franja**; el planner las resuelve con las **intenciones de paleta** que ya existen (`CopperIntent` `PaletteLine`/`PaletteSpan` + `Plan::add_prioritized`, que fusiona conflictos por línea).
- Conflictos: mismo par de canales con paletas distintas en líneas solapadas → reasignar canal si hay hueco; si no, **degradar el de menor `z` a BOB** (que tiene paleta propia de playfield) o cambiar el color por Copper en la línea de entrada (si el arte lo permite).
- Con la escena: el arbitraje usa la misma maquinaria de zonas de paleta (`PaletteZone`/`PatchZone` por frame) para que los cambios de `COLOR16-31` sean compatibles entre playfield y sprites (especialmente con 5+ planos).
- Piezas existentes: `HwSpritePaletteSwitch`/`SpritePaletteEvent` (plantilla por franja), `CopperIntent`/`Plan`, `Palette`/`Palette32`/`PatchZone`. Huecos: `SpriteIntent` no lleva requisito de paleta y el reparto del par es manual.

**Verificación:** HOST del arbitraje (dos sprites de paletas distintas en el mismo par → reasigna o degrada; misma franja con Copper → convive), + demo con sprites de paletas distintas arriba/abajo y fondo de sprites con conmutación por frame.

### F8. Secuenciador de eventos y animaciones (timeline)

**Objetivo.** Poder secuenciar de forma genérica (shmup por oleadas, cutscenes, cámara, UI) una línea temporal de **pistas de valores con interpolación** y **pistas de eventos**, determinista por ticks de juego y sin asignación dinámica.

**Dónde va.** No es `eng::sim` (ese dominio es el ecosistema de criaturas/IA) ni `eng::ai` (decisión/planificación): es un algoritmo **genérico de dominio**, así que corresponde a `engine/include/eng/core/` (p. ej. `core/seq/sequencer.hpp`), con integración de juego en `eng/scene`/fachada. Piezas a reutilizar: `core/math/interp.hpp` (lerp/easings), `core/math/spline.hpp` (tracks de posición), `core/util/{state_machine,event}.hpp`, y el concept `Effect` (`update`/`apply_into`) como puente al frame.

**Contrato extraído (adaptado al estilo del engine):**

```cpp
// Propuesta: pistas de capacidad fija; `S` = escalar del valor (genérico, §1.11).
template <typename S, u8 MaxKeys>
struct KeyTrack {
    struct Key { u16 tick = 0; S value {}; u8 ease = 0; }; // ease: id de core/math/interp
    Key keys[MaxKeys] {};
    u8 count = 0;
    S sample(u32 tick) const;               // interpolación entre claves (sin heap)
};

template <u8 MaxEvents>
struct EventTrack {
    struct Event { u16 tick = 0; u16 id = 0; }; // id interpretado por el juego
    Event events[MaxEvents] {};
    u8 count = 0;
};

template <u8 MaxTracks, u8 MaxKeys, u8 MaxEvents>
struct Sequence {
    KeyTrack<s16, MaxKeys> value_tracks[MaxTracks] {};   // posición, escala, velocidad…
    EventTrack<MaxEvents>  event_tracks[MaxTracks] {};
    u16 length = 0;          // ticks totales
    bool loop = false;
};

struct SequenceRunner {
    u16 tick = 0;
    bool playing = false;
    bool finished = false;
    // `advance` devuelve los eventos disparados este tick (sin cola: callback o span).
    template <class OnEvent> void advance(const Sequence<...>&, u16 ticks, OnEvent on_event);
    void seek(const Sequence<...>&, u16 tick);
};
```

- **Determinista**: avanza con los mismos ticks que `actor_tick`/`composite_advance`; `seek` permite depurar/repetir y el host puede cocinar la secuencia.
- **Genérico**: una pista puede animar los puntos de control de una ruta (F2), la posición de una cámara (`route_camera`) o la paleta; los eventos disparan spawns de formaciones, música o cambios de estado de juego.
- **Datos**: las secuencias se cocinan a `constexpr`/tables desde un formato de autoría (futuro cooker), como el resto de assets.

**Verificación:** HOST (avance con `ticks` múltiples, `seek`, `loop`, orden de eventos, fin de secuencia) + demo de nivel/oleada dirigida por timeline (spawner F2 + rutas F2) con gate visual.

### Orden recomendado

F1 → F2 (o F2 → F1 si el primer juego objetivo es un shmup) → F8 si hay niveles/oleadas que secuenciar → F5/F6 según el juego → F7 cuando aparezcan 5+ planos o sprites con paletas por zona → F3/F4 como optimización y cierre de técnicas. F3 **no** bloquea a las demás: el camino de rearme por Copper actual es correcto y suficiente.

## 6. Riesgos y protocolo de verificación

- **Nada de afirmaciones de hardware sin fuente**: el encadenado DMA (F3), el pacing Jim Power (F4) y el bending (F4) se contrastan con `../WinUAE-DBG/` (p. ej. `drawing.cpp` para el fetch de sprites y `custom.cpp` para los registros) y el AHRM, citando `fichero:línea` en el código y en la ficha (`AGENTS.md` §1.12). El formato de estructura DMA ya está documentado en `docs/reference/emulators/winuae/sprite-dma.md`.
- **Regla de oro visual**: toda fase que cambie lo que se ve (compuestos, trayectorias, mecanismos, drivers) se valida con secuencia + modelo de visión local (Ollama), no con una captura estática (`DEMO_VISUAL_DEBUG.md`).
- **Presupuesto**: cada fase mide fps/ciclos por frame y compara A/B cuando sustituya un camino (p. ej. DMA vs rearme). Un tic o pico de frame se perfila antes de dar la fase por buena.
- **Coste previsible**: los contenedores son de capacidad fija y plantilla (`EntityPool<Max>`, `Span`), sin heap ni `std::function`/virtual en el bucle de juego. Las tablas (seno, formaciones) se precalculan en setup o `constexpr`.

## 7. Criterios de aceptación (por fase)

1. Contrato en el engine con **test host** determinista (y con dos escalares si la cabecera fuese genérica).
2. **Demo** en `demos/` que consuma la fachada (`eng/api`), sin nombrar registros ni tipos del backend, con gate visual (secuencia + Ollama) y cadencia verificada.
3. Sin regresiones: `tools/test-regression.sh` en verde y sin flicker en las demos comparadas.
4. Documentación: contrato en `docs/engine/architecture/` (o ficha de técnica en `docs/reference/amiga/techniques/`), y **una** lista de estado (la del `ROADMAP_UNIFICADO.md`), sin duplicar tablas.

## 8. Referencias

- Contratos: `OBJECT_SYSTEM.md`, `SPRITE_CHANNEL_WINDOWS.md`, `VISUAL_EFFECT_SPRITE_DESIGN.md`, `CONTENT_AND_TILEMAP.md`, `PIXEL_ART_2D_ISOMETRIC.md` (§5-§7: movimiento, mecanismos, sólidos).
- Técnicas: `sprite-techniques-catalog.md`, `sprite-tricks-games.md`, `sprite-layer.md`, `sprite-horizontal-multiplex.md`, `sprite-multiplexer-bob-fallback.md`, `dual-playfield-fastbobs.md`, `interleaved-bob-single-blit.md`.
- Emulador: `docs/reference/emulators/winuae/sprite-dma.md`, `sprite-color-priority.md`; AHRM 3.ª cap. 4 (sprites) y cap. 7 (`BPLCON2`, `CLXCON`/`CLXDAT`); *CPU Blit Assist* (Power Programs) para el camino de Sprite CPU (A1200).
- Reutilización en el engine: `core/math/spline.hpp` (Bézier/Hermite/Catmull-Rom genéricos), `core/math/interp.hpp` (lerp/easings), `core/math/isqrt.hpp`, `core/math/sinetable.hpp`, `core/util/{state_machine,event}.hpp`, `scene/representation.hpp` (`Representation`), `graphics/copper/plan.hpp` (conflictos por línea), `scene/route_camera.hpp`.
- Estado vivo: `ROADMAP_UNIFICADO.md` §«Sprites hardware — estado»; fachada `eng/api/sprites.hpp`; metódica `DEMO_VISUAL_DEBUG.md` y `docs/testing/README.md`.
