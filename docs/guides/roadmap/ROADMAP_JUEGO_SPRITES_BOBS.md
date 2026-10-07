# Sprites hardware y BOBs para videojuegos: análisis de adecuación y roadmap

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
| 10. Cambios de color por Copper (palette splitting por franjas) | Parcial: `HwSpritePaletteSwitch` de plantilla | F4 (demo y generalización) |
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

## 5. Roadmap de implementación

Orden por **dependencia y valor para juego**. Cada fase declara objetivo, contrato extraído (adaptado al estilo del engine: sin heap, sin RTTI, sin STL, tipos `u8`/`s16`/`Span`/`Ref`), encaje y verificación.

### F1. Actores compuestos (personajes, vehículos, jefes)

**Objetivo:** describir un objeto como **varias partes** (piernas, torso, arma, rueda, aura) con animación **por parte** y secuencias que cambian por condiciones de juego, con ancla y **cajas de golpe** por frame. Es el ladrillo que más acerca el engine a un juego (el ejemplo clásico: piernas por un lado y resto del cuerpo por otro).

**Contrato (contenido inmutable, cocinable en host):**

```cpp
// eng/graphics/composite_visual.hpp (propuesto)
struct HitBox { Box box {}; u8 group = 0; bool solid = true; };

struct CompositePart {
    Visual visual {};        // Bob o HardwareSprite: mismos caminos de §2
    s16    offset_x = 0;     // respecto al ancla del actor
    s16    offset_y = 0;
    u8     z = 0;            // orden de dibujo dentro del compuesto
    bool   attach = false;   // parte HW a 15 colores (par attached)
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

**Materialización (tipos concretos, sin virtual en el camino caliente):** dos funciones libres que leen el mismo estado; las partes se filtran por `VisualKind`:

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

**Generadores** (setup/constexpr; la tabla de seno **ya existe**, no se añade otra): `eng/core/math/sinetable.hpp` (`SineTable<Amp, Steps>`, generada en compilación) y `eng/retro/sintab.hpp` (la 4.12 exacta, `kSinTab`); el coseno es la misma tabla con la fase desplazada un cuarto de período.

```cpp
u16 gen_line(Span<PathPoint> out, s16 dx, s16 dy, u16 length, u16 ticks_per = 1);
u16 gen_sine_vertical(Span<PathPoint> out, s16 amplitude, s16 y_step, u16 length,
                      u8 phase0 = 0, u16 ticks_per = 1);
```

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

### Orden recomendado

F1 → F2 (o F2 → F1 si el primer juego objetivo es un shmup) → F5/F6 según el juego → F3/F4 como optimización y cierre de técnicas. F3 **no** bloquea a F1/F2: el camino de rearme por Copper actual es correcto y suficiente para ambos.

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
- Emulador: `docs/reference/emulators/winuae/sprite-dma.md`, `sprite-color-priority.md`; AHRM 3.ª cap. 4 (sprites) y cap. 7 (`BPLCON2`, `CLXCON`/`CLXDAT`).
- Estado vivo: `ROADMAP_UNIFICADO.md` §«Sprites hardware — estado»; fachada `eng/api/sprites.hpp`; metódica `DEMO_VISUAL_DEBUG.md` y `docs/testing/README.md`.
