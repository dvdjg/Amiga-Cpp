# Sistema de sprites HW: evaluación de la consulta y plan de implementación detallado

Este documento evalúa **punto por punto** la consulta externa sobre el sistema de sprites (inventario de técnicas, Free Form, organización de memoria, Jim Power, bending por tabla de seno, reparto sprite-vs-BOB con ocupación por línea y DMA encadenado, sprites compuestos y materializadores, FastCopy de dual playfield, trayectorias/formaciones/pool de shmup, cuerdas/muelles/catapultas/puertas/destructibles) y fija las **instrucciones precisas para cerrar lo que falta**, con los contratos extraídos adaptados a las reglas del engine.

Relación con los documentos vigentes (fuente única por tema, sin repetir su contenido aquí):

- **Estado y prioridad vivos**: `ROADMAP_UNIFICADO.md` §«Sprites hardware — estado» (única lista de pendientes).
- **Análisis de adecuación y fases F1–F8**: `ROADMAP_JUEGO_SPRITES_BOBS.md`. Sus fases **F1** (compuestos), **F2** (trayectorias/formaciones/pool) y **F8** (timeline) ya están **hechas y validadas con demo**; este documento detalla las abiertas (F3–F7) y añade las tareas de cierre extraídas de la consulta.
- **Free Form medido**: `ROADMAP_218_CAMPO_UNICO.md` (campaña en curso) y `BUS_BUDGET.md` (modelo de bus medido).
- **Contratos de técnica**: `sprite-layer.md`, `sprite-horizontal-multiplex.md`, `sprite-techniques-catalog.md`, `sprite-tricks-games.md`, `sprite-multiplexer-bob-fallback.md`, `SPRITE_CHANNEL_WINDOWS.md`, `OBJECT_SYSTEM.md`, `PIXEL_ART_2D_ISOMETRIC.md` §5–§7.
- **Fuente del hardware**: `winuae/sprite-dma.md` (estructura DMA y columna fantasma) y AHRM 3.ª cap. 4; en este documento toda afirmación de mecanismo cita `fichero:línea` de `../WinUAE-DBG/` o el AHRM (AGENTS §0 y §1.12).

## 1. Mapa: qué está hecho y qué abre este plan

```text
  contenido                  reparto                      emisión                         hardware
  ─────────                  ───────                      ───────                         ────────
  Visual / CompositeVisual ─► SpriteAllocator ────► HwSpritePlacement ──► ┌─ rearme por Copper (HECHO) ─► SPRxPT/POS/CTL
  (F1 HECHO: partes,          (HECHO: first-fit,      (compose_sprites,    │   emit_placements_into
   secuencias, hitboxes)       bitfield por línea,      pares attached      │   (multiplexado vertical)
                               grupos, tiras,           cocinados)         └─ cadena DMA (F3, ESTE PLAN) ─► SPRxPT → [POS,CTL,DATA]…
                               cadenas, ventanas,                                                      (cero MOVEs por sprite)
                               ledger de fondo)

  Emisión por línea/fondo:   SpriteLineLayer (HECHO) · Risky Woods (HECHO) · Free Form (F4.1, campaña 218)
  Efectos por línea:         bending (F4.2, NUEVO) · paleta por franja (F4.3/F7) · Jim Power DATA (F4.4, pacing ABIERTO)
  Juego:                     trayectorias/formaciones/pool (F2 HECHO) · mecanismos de escenario (F6, NUEVO)
```

## 2. Evaluación punto por punto de la consulta

Resumen de veredictos: **cubierto** (existe y está verificado por test host y/o demo), **adaptar** (la idea entra, con cambios), **condicionado** (solo con prerrequisito de medida o cierre pendiente), **descartado** (no se implementa tal cual, con motivo).

### 2.1 Inventario de técnicas (bloque 1 de la consulta)

| Técnica de la consulta | Veredicto | Pieza / fase del engine |
|---|---|---|
| 1. Sprites como objetos de juego (jugador, enemigos, balas, HUD) | **Cubierto** | `SpriteScene` / `compose_sprites`; demos 216/217 |
| 2. Attached (15 colores) | **Cubierto, con ajuste** | `cook_attached_pair` + allocator de pares (HOST-427/428; demos 214/216). El par **se deriva** del contenido (4 planos y `w <= 16`, `visual_is_attached_pair`): no se añade flag `wantsAttached` (ver §2.8) |
| 3. Multiplexado vertical (reuso del canal en líneas distintas) | **Cubierto** | `SpriteAllocator` (first-fit + bitfield) + `emit_placements_into`; gap de 1 línea = `kSpriteVerticalGapLines` |
| 4. Multiplexado horizontal con Copper (media línea) | **Parcial** | `SpriteLineLayer` (HUD) y Risky Woods (208/211) hechos; Free Form → F4.1 |
| 5. Fondo repetitivo Risky Woods | **Cubierto** | `effects::RiskyWoodsLayer` (HOST-417; demos 208/211) |
| 6. Free Form Sprite Layer sin patrón | **Condicionado** | `effects::FreeFormSpriteLayer` (HOST-419; demo 213); cierre por la campaña 218 → F4.1 |
| 7. Combinación sprites + BOBs | **Cubierto** | Degradación `as_bob` + `emit_bob_fallbacks`; `FastBobLayer` en DPF (HOST-355; demo 213) |
| 8. Colisiones (hardware `CLXCON`/`CLXDAT` y software) | **Cubierto** | `sprite_collision.hpp` (demo 206) + hitboxes por frame (`composite_hitboxes`, F1) |
| 9. Efectos de Copper sobre `SPRxPOS` por fila (bending) | **Adaptar** → F4.2 | Driver de bending reutilizando `core/math/sinetable.hpp`; el pacing de Jim Power (215) es la misma familia de problema |
| 10. Cambio de color por Copper (palette splitting) | **Adaptar** → F4.3/F7 | `HwSpritePaletteSwitch`, `SpritePaletteEvent`, planificador `sprite_palette.hpp` (HOST-433); faltan integración + demo |
| 11. Avanzadas (modo manual, chasing extremo, parallax, HUD por sprites) | **Cubierto en su mayoría** | `SpriteHorizontalRearm`, `chain_*`, `SpriteLineLayer`; lo que la consulta llama «chasing» es el rearme por franja ya validado (053/217) |

### 2.2 Free Form, organización de memoria y Jim Power (bloques 2 y 3)

| Propuesta | Veredicto | Detalle |
|---|---|---|
| 8 sprites = 128 px + reposición por Copper cada ≥24 px | **Cubierto** | `kSpriteMinReusePx = 24` (`sprite_limits.hpp:35`) ya es la constante de la carrera contra el haz; `SpriteHorizontalRearm` modela el rearme horizontal |
| Estructura de copperlist por línea: 1 WAIT + ~41 MOVE | **Adaptar** | El emisor es el `Scheduler`; los números de coste salen del modelo medido (`BUS_BUDGET.md`, `eng/hw/bus_budget.hpp`), no de estimaciones del hilo |
| Scroll A: regenerar datos shifteados; B: posiciones + datos diferidos con 4 copperlists y swap cada 32 frames | **Condicionado / adaptar** | Es una variante del diseñado en la campaña 218; solo si la medida la admite (el esquema completo son ~152 KB de copperlists: viable en 1 MB, no en el A500 base con bitplanes). La actualización por **columnas contiguas** (un blit = una columna de altura) se adopta como nota de diseño en F4.1 |
| Actualización de posiciones en 4 frames y datos en 32 | **Descartado como regla general** | La cadencia de actualización debe salir de la campaña 218 medida; un reparto fijo de frames es específico de aquel efecto |
| Jim Power: Copper escribe `SPRxDATA`/`DATB` a media línea | **Condicionado** → F4.4 | La estructura está clara, pero el **pacing** está abierto (demo 215 NO VERIFICADA; consulta de seguimiento enviada). No se reimplementa desde el ejemplo genérico del hilo |
| Organización de memoria (2 buffers de sprites, 4 copperlists, 1 MB Chip) | **Adaptar** | Los buffers DMA van tipados `ChipView<SpriteTag>` (AGENTS §1.10) y dimensionados en el `AssetRuntime`; las cifras de memoria se calculan por efecto, no se fijan en el contrato |
| Bending por tabla de seno (precalcular, parchear fase por frame) | **Adaptar** → F4.2 | Reutiliza las tablas existentes (`core/math/sinetable.hpp`, `kPathSine` de `scene/trajectory.hpp`); el parcheo por frame de `SPRxPOS` es exactamente el driver de F4.2 |
| Bending avanzado (por sprite, con DATA, vertical, doble onda) | **Adaptar** | «Simple» y «por sprite» entran en F4.2; «con DATA» queda ligado a F4.4 (pacing); «vertical (módulo)» se descarta (técnica exótica, sin caso de juego) |

### 2.3 Reparto sprite-vs-BOB (bloques 4–6)

| Propuesta | Veredicto | Detalle |
|---|---|---|
| Algoritmo: ordenar por Y + first-fit con `lastY[8]` | **Cubierto y superado** | La versión `lastY` requiere intents ordenados; el engine usa **ocupación exacta por línea** (`sprite_allocator.hpp:84`, bitfield 256 bits/canal) que además permite pasadas por prioridad sin perder reutilización (HOST-003) |
| Versión con bitfields (`std::bitset`/máscaras de 32 bits) | **Cubierto** | El allocator ya es la versión con arrays fijos de `u16` (sin STL); el código propuesto queda como referencia, no se añade |
| Grupos de trayectoria reservando canales contiguos | **Cubierto** | `SpriteIntent::group_id/group_index/group_span` + corrida contigua o grupo entero a BOB (HOST-003) |
| Ristra vertical de tiros = 1 canal | **Cubierto** | El allocator reutiliza el canal; `emit_placements_into` rearma por franja (HOST-428). La cadena DMA (F3) es la optimización siguiente |
| Prioridad de asignación (fijos antes que libres) | **Cubierto** | `assign_rank` 0..3, pasadas 3→0 |
| «Flicker controlado» (el que no cabe va a BOB y se prioriza luego) | **Descartado en el engine** | Es política de la app, no del allocator; la degradación `as_bob` ya es determinista y testable |
| `SpriteAllocator`/`PreciseSpriteAllocator` con `std::vector`/`std::sort`/`std::bitset` | **Descartado** | STL y heap prohibidos (CODING_STYLE); el orden por `top` lo garantiza el productor (y su versión incremental es F4.5) |
| Builder `SpritePlacementBuilder` (intents + slots + `SpriteImageRef` → placements) | **Cubierto** | `compose_sprites` publica `HwSpritePlacement` con DATA `ChipView` y cocina los pares *attached*; no se añade un builder paralelo |
| **Lista DMA encadenada por canal** (sin rearme de Copper) | **Adaptar** → **F3** (el grueso de este plan) | El formato `[POS,CTL,DATA…][POS,CTL,DATA…]…0,0` encaja con el walk del DMA documentado en `winuae/sprite-dma.md`; contrato adaptado al engine en §3.2 |
| `SpriteManager::set_from_dma_lists` (array de spans de words) | **Adaptar** | En el engine no se registran spans sueltos: la cadena es un `ChipView<SpriteTag>` y el armado un `arm_dma_chain` estático (2 MOVEs de `SPRxPT`), §3.2 |
| Híbrido: primer uso por DMA, siguientes por rearme de Copper | **Adaptar** | Es el **selector** de estrategia por canal (§3.2.4): `Rearm` (camino actual, por defecto), `Chain` (cadena reconstruida), `ChainPersistente` (parcheo de control words) |
| Selector automático con umbrales «4 reusos → PureCopper» | **Adaptar con medida** | Los umbrales del hilo son heurística; el criterio del engine es de coste explícito (words copiadas vs MOVEs ahorrados) con el presupuesto de `BUS_BUDGET.md` |

### 2.4 Compuestos y materializadores (bloques 7–9)

| Propuesta | Veredicto | Detalle |
|---|---|---|
| `CompositeVisual`/`CompositeState` (partes, secuencias, ancla, hitboxes) | **Cubierto** | `graphics/composite_visual.hpp` + `scene/composite_actor.hpp` (F1, HOST-429); demo 063 validada con visión |
| Materializer HW (intents por parte) | **Cubierto, con ajuste** | `composite_to_sprite_intents` + `composite_part_data`; el par *attached* se emite como **dos** intents (derivado), no con flag |
| Materializer BOB con espejo y `z` | **Cubierto, con ajuste** | `composite_emit_bobs` + `composite_part_box` (espejo calculado en la caja); la representación por parte la decide el llamador con `Span<const Representation>` (mezcla HW + BOB en el mismo personaje) |
| `ICompositeMaterializer` (interfaz virtual) | **Descartado** | Sin virtuales en el camino caliente (CODING_STYLE); funciones libres con tipos concretos |
| Políticas `bob_draw`/`bob_erase` por parte (override en `CompositePart`) | **Descartado tal cual** | Contradice la frontera contenido/materialización (ROADMAP_JUEGO_SPRITES_BOBS §4.5): la política BOB vive en `ActorDesc`/`TransparencyMode`/`resolve_background`; si un juego pide política por parte, se **añade como parámetro del materializador** (un `Span<const BobPolicy>` paralelo a `part_repr`), nunca al contenido |
| `prev_x[]`/`prev_y[]` por parte para borrado | **Adaptado** | El borrado es política del llamador (`BobLayer`, save-under); el pool ya guarda `prev_x/prev_y` por entidad |
| `frame_stride` mal aplicado (`part.visual.bitplanes` como stride) en el código del hilo | **Corregido** | En el engine `composite_part_data` usa `frame_stride` en bytes con recorte de rango (HOST-429); el ejemplo del hilo se ignora |
| Animación por ticks con `advance`/`set_sequence`/eventos | **Cubierto** | `composite_advance`/`composite_set_sequence` (enteros, O(1) amortiguado) |

### 2.5 FastCopy / dual playfield fast BOBs (bloque 10)

| Propuesta | Veredicto | Detalle |
|---|---|---|
| Copia con padding que dibuja y limpia en un blit (`$F0`, sin máscara) | **Cubierto** | `BobDraw::Opaque` (copia `$F0`) + `FastBobLayer` con degeneración a cookie-cut + clear (HOST-355) |
| Nuevo `BobDraw::FastCopy` | **Descartado** | Duplicaría `BobDraw::Opaque`; el enum no se toca |
| `allow_fast_copy` + `dual_playfield_front` en el contenido | **Descartado tal cual** | La decisión es de la capa (`FastBobLayer` y el destino), no del contenido; el flag de escena ya existe implícito en la política del layer |
| Márgenes transparentes por velocidad máxima, doblados con doble buffer, calculados en preproceso | **Adaptar** → **F5** | Es exactamente lo pendiente de F5: el asset declara velocidad máxima por eje y el pipeline host calcula márgenes y tamaño final (§3.4) |
| Orden de pases: primero fast-copy, luego cookie-cut | **Cubierto** | Es el reparto de `FastBobLayer` (los solapados degradan a cookie-cut + clear) |

### 2.6 Trayectorias, formaciones y pool (bloques 11–13)

| Propuesta | Veredicto | Detalle |
|---|---|---|
| `Trajectory`/`TrajectoryFollower` + `PathPoint` + `gen_line`/`gen_sine_vertical` | **Cubierto, con ajuste** | `scene/trajectory.hpp` (F2, HOST-430), genérico sobre el escalar (AGENTS §1.11); la tabla de seno es `core/math/sinetable.hpp` (`SineTable<127,256> kPathSine`), no una copia de 256 bytes |
| Rutas spline (Bézier/Hermite/Catmull-Rom) | **Cubierto** | `RouteSpline` + `route_sample`/`route_tangent` reutilizando `core/math/spline.hpp` (F2) |
| `Formation`/`FormationMember`/`formation_update` con delays | **Cubierto** | `scene/formation.hpp` (F2); demo 064 (V en espejo) |
| Generador de ola en **V** (`gen_formation_v`) | **Adaptar** → F2.1 | No existe aún en el engine: es la única pieza de este bloque que queda por añadir (§3.1) |
| `EntityPool` con trayectoria + compuesto + `prev_x/prev_y` + culling | **Cubierto** | `scene/entity_pool.hpp` (F2); campos `damage`/`faction`/`vertical_stream` ya presentes |
| Integración con `TrajectoryGroup` en el allocator | **Cubierto** | `group_*` en `SpriteIntent`; el juego las recolecta y les asigna el mismo `group_id` |
| Colisión entidad-entidad sobre el pool | **Aplazado** | Fuera del alcance de sprites: usa las primitivas de `core/util/collision.hpp` y el broadphase; el pool ya expone las posiciones |

### 2.7 Mecanismos de escenario (bloques 14–15)

| Propuesta | Veredicto | Detalle |
|---|---|---|
| Cuerda/puente por eslabones con verlet (integrate + constraints, damping) | **Adaptar** → **F6** | Contrato en §3.6; usa `isqrt` del engine para la restricción (determinista) con la aproximación sin `sqrt` como variante opcional medida |
| Emisión de eslabones como compuestos/BOBs | **Adaptar** | Un `CompositeVisual` de 1 parte por eslabón + `composite_emit_bobs`; en DPF puede usarse `FastBobLayer` (F5) |
| Colisión jugador ↔ puente (proyección sobre el segmento + impulso + adhesión) | **Adaptar** → F6 | `rope_find_contact`/`rope_resolve_player` adaptados (§3.6) |
| Muelles (compresión + boost de retorno) | **Adaptar** → F6 | Máquina de estado corta; comparte `ActorFeet` |
| Catapulta articulada (compuesto multi-secuencia + tip por seno + spawn en el extremo) | **Adaptar** → F6 | Encaja con F1 (secuencias por rango de ángulo) y con `kPathSine` para el tip |
| Puertas, destructibles y **sólidos unificados** AABB | **Ya diseñado** | `PIXEL_ART_2D_ISOMETRIC.md` §7 + `core/util/collision.hpp`; no se duplica aquí. F6 solo añade la interacción cuerda ↔ jugador y la catapulta |
| Escombros con trayectoria al destruir | **Cubierto** | `EntityPool::spawn` + `Trajectory` (F2) más el diseño de destructibles existente |

### 2.8 Descartes razonados (lo que no se implementa)

1. **Código con STL, heap o virtuales en el camino caliente** (`std::vector`, `std::sort`, `std::bitset`, `ICompositeMaterializer`, `std::function`): prohibido por `CODING_STYLE.md`; el engine ya tiene su versión fija y sin heap de todo lo que ese código proponía.
2. **Flags de hardware en el contenido** (`wantsAttached` en el objeto, `bob_draw`/`bob_erase` en `CompositePart`, `allow_fast_copy` en el `Visual`): la frontera contenido/materialización está fijada en `ROADMAP_JUEGO_SPRITES_BOBS.md` §4.5; las decisiones van al planner o a la capa de juego.
3. **`BobDraw::FastCopy` como valor nuevo**: duplica `BobDraw::Opaque`; el trabajo real es el preproceso de márgenes (F5).
4. **`lastUsedY[8]` como ocupación**: superado por el bitfield exacto (que además permite grupos/tiras/cadenas y pasadas por rango).
5. **El esquema completo de 4 copperlists de Free Form como contrato**: se mantiene como **variante medida** de F4.1; no se fija en el contrato (memoria y cadencia dependen del efecto y de la campaña 218).
6. **Reimplementar el pacing de Jim Power desde el ejemplo genérico**: no aporta la pieza que falta (el adelanto del Copper sobre el haz con 4 planos); se mantiene la investigación abierta (demo 215) y su fallback por columnas (213).

### 2.9 Ajustes transversales aplicados a toda la implementación

- **Memoria DMA tipada por banco**: todo buffer que Agnus lea (cadenas, estructuras, DATA de eslabones si van por DMA) llega como `ChipView<SpriteTag>`/`Block<…, MemoryKind::Chip>`; un `Span` suelto que termine en DMA no compila (AGENTS §1.10).
- **Cero heap, capacidad fija**: buffers del llamador (`ChipView` + cursor `used`), arrays `eng::util::Array`, plantillas de capacidad; sin `new`/STL (AGENTS §1.10, CODING_STYLE).
- **Reutilizar antes de crear**: tablas de seno (`SineTable`/`kPathSine`), `isqrt`, splines, `collision.hpp`, `FastBobLayer`, planificador de paleta, `Sweep`/bitset util del engine (AGENTS §1.7).
- **Coste medido, no estimado**: cada fase con impacto en el frame se contrasta con `BUS_BUDGET.md` + `tools/debug/measure-fps.mjs` + perfil `.amigaprofile` (AGENTS §1.15); los umbrales del selector F3 salen de esa medida.
- **Afirmaciones de hardware con fuente**: F3/F4.1/F4.2/F4.4 citan `../WinUAE-DBG/` y AHRM; sin demo A/B que lo valide, la fase queda NO VERIFICADA (AGENTS §0, §1.12).
- **Una sola lista de estado**: al cerrar cada fase se actualiza `ROADMAP_UNIFICADO.md` §«Sprites hardware — estado»; este documento aporta el detalle y no duplica tablas de estado (AGENTS §1.3).
- **Numeración**: toda demo/tool nueva se numera con `node tools/check/next-number.mjs demos/techniques/amiga/sprites` (y `tests/host/…` según catálogo) antes de crearla (NUMBERING.md).

## 3. Fases abiertas: instrucciones de implementación

Cada fase declara objetivo, contrato, ubicación, algoritmo, tests host, demo y criterio de salida. Las fases **F1/F2/F8** ya cerradas no se repiten.

### 3.1 F2.1 — Generadores de formación (V clásica y V con seno en espejo)

**Objetivo:** añadir a `scene/formation.hpp` los dos generadores de la consulta que faltan; el resto del bloque F2 está hecho.

**Contrato (genérico sobre el escalar `S`, patrón del repo):**

```cpp
// eng/scene/formation.hpp (extensión)
/// Ola en V: `count` miembros repartidos con separación `spread_x`, retraso `delay_step`
/// por distancia al centro y una trayectoria común. `open_down = false` deja la punta
/// arriba (entran en V invertida desde arriba de pantalla); `true`, punta abajo.
template <typename S>
inline u16 gen_formation_v(eng::Span<FormationMember<S>> out, u8 count,
                           eng::util::type_identity_t<S> spread_x, u16 delay_step,
                           u8 trajectory, bool open_down = false) noexcept;

/// Variante «brazos que ondulan en espejo»: cada mitad usa una trayectoria
/// (p. ej. la misma seno con fase 0 y fase 128).
template <typename S>
inline u16 gen_formation_v_sine(eng::Span<FormationMember<S>> out, u8 count,
                                eng::util::type_identity_t<S> spread_x, u16 delay_step,
                                u8 trajectory_left, u8 trajectory_right) noexcept;
```

**Algoritmo:** `dist = |i - count/2|`; `delay_ticks = dist * delay_step` (la punta entra primero); `offset.x = (i - count/2) * spread_x`; `offset.y = ±dist * (spread_x / 2)` para abrir la V; `trajectory` por miembro (la variante sine alterna `trajectory_left/right`). Casts a `s16` antes de multiplicar (u8 no resta bien).

**Tests host:** conteo exacto; delays monótonos hacia los extremos; offsets simétricos; `count = 0`/`1`; buffer pequeño (tope por `out.size()`).

**Demo:** ampliar `064_shmup_wave` (o nueva si el cambio de arte lo pide) usando el generador en lugar de la tabla a mano.

**Criterio de salida:** HOST nuevo en verde; la demo mantiene factura visual y cadencia (gate Ollama + fps).

### 3.2 F3 — Emisión DMA encadenada, híbrido y selector

**Objetivo:** cuando un canal sirve **varios placements en líneas disjuntas** (multiplexado vertical, ristras de tiros, grupos), sustituir los rearmes de Copper (un `WAIT` + 4 MOVEs por reaparición) por **una estructura encadenada en Chip** que Agnus recorre sola: `[POS,CTL,DAT/DATB…][POS,CTL,DAT/DATB…]…[0,0]`. El camino actual (rearme) se mantiene como estrategia por defecto.

**Base del mecanismo (verificada en la fuente; citar en comentario y commit):**

- El DMA de un canal lee `POS` y `CTL` de la memoria apuntada por `SPRxPT` cuando `dmastate == 0`, y **procesa la cabecera** (arranque por `VSTART`, parada por `VSTOP`): `custom.cpp:4078-4090` (`SPRxCTL_DMA`: fuerza `dmastate=0` y llama `sprstartstop`) y `custom.cpp:4031-4042` (`sprstartstop`: `vpos == vstart` → on; `vpos == vstop` → off).
- El puntero **avanza** con cada fetch (`r->pv += sprite_width / 8`; `s->pt = r->pv`, `custom.cpp:12012-12023`), y el canal ocioso sigue consumiendo words de la estructura (`generate_sprites`, `custom.cpp:10055-10120`). Al terminar un bloque, la pareja `POS/CTL` del bloque siguiente es la que arma la siguiente franja: **eso es la cadena**.
- El `SPRxPT` debe **recargarse cada frame** al inicio de la cadena (sin recarga, el walk queda tras el terminador — antipatrón *Phantom Sprite*, `sprite-dma.md` y comentario de `cook_attached_pair`). La carga con `VSTOP <= VSTART` (pareja `0,0`) no arma pantalla; por eso el buffer se pone a cero y el walk tras el terminador solo lee ceros.
- Cabecera **OFF** (`kSpriteOffPos`/`kSpriteOffCtl`, `sprite_limits.hpp:52-53`) para el modo mixto Copper+cadena: un canal que lea una cabecera OFF no se auto-arma.

```text
  canal 3, frame N:                     SPRxPT ─┐
  arm_line (1 WAIT + 2 MOVEs) ────────────────► │
                                                ▼
  Chip RAM:  [POS,CTL][DAT DATB × h₁][POS,CTL][DAT DATB × h₂][POS,CTL][DAT DATB × h₃][0,0][0,0]…
              bloque 1 (tiro a)        bloque 2 (tiro b)        bloque 3 (tiro c)     terminador + runway
              └── el walk del DMA consume cabecera y datos de cada bloque en su franja ──┘
```

#### 3.2.1 Contrato de la cadena (`eng/graphics/sprite_chain.hpp`, propuesto)

```cpp
/// Estructura DMA encadenada de UN canal, en Chip RAM del llamador.
struct SpriteDmaChain {
    eng::ChipView<eng::SpriteTag> storage {};  // buffer Chip; se pone a CERO al reservar (runway)
    u16 used = 0;                              // words escritas (sin contar el runway)
    eng::util::Array<u16, 16> block_pos_words {};  // offset (words) de la POS de cada bloque
    u8 block_count = 0;
};

/// Codificación POS/CTL compartida con SpriteManager (misma convención que
/// `SpriteManager::emit_config`: AHRM cap. 4; bits verificados en `custom.cpp:4044-4058`).
struct SpritePosCtl { u16 pos; u16 ctl; };
[[nodiscard]] constexpr SpritePosCtl encode_sprite_pos_ctl(u16 hpos, u16 vstart, u16 height,
                                                           bool attach) noexcept;

/// Construye la cadena de los placements del MISMO canal (orden no decreciente de
/// `vstart`, sin solape y con el gap ya garantizado por el allocator). Escribe
/// `POS,CTL` + `height*width_words*2` words de DATA por bloque + terminador 0,0.
/// `block_pos_out` (opcional) recibe el offset de la POS de cada bloque (para parcheo).
/// Devuelve false sin escribir nada si no cabe o algún placement es inválido.
[[nodiscard]] bool build_sprite_dma_chain(eng::Span<const HwSpritePlacement> channel_placements,
                                          SpriteDmaChain& out,
                                          eng::Span<u16> block_pos_out = {}) noexcept;
```

**Reglas del builder (todas testables en host):** posiciones ascendentes y sin solape (si no, devolver `false`); `height` recortado a 128; `data` no vacío y con words suficientes (`height*2*width_words`); `attach` solo en bloque de canal impar (bits del CTL); terminador `0,0` y **no tocar el resto del buffer** (el runway ya está a cero); todo-o-nada por canal (si no cabe, el selector usa `Rearm`).

**Extraer y compartir `encode_sprite_pos_ctl`**: hoy la codificación vive privada en `SpriteManager::emit_config` (`sprite_manager.hpp:396-411`); al crear el builder se **extrae a función libre** y ambos la usan (AGENTS §1.7: nada de dos codificaciones).

#### 3.2.2 Armado de la cadena (extensión de `SpriteManager`)

```cpp
/// Rearma el canal al inicio de la cadena: 1 WAIT + 2 MOVEs (SPRxPTH/PTL) en `arm_line`
/// (línea temprana, antes del primer `VSTART`, tras el VBlank — patrón de 214).
/// El DMA lee POS/CTL de la cabecera (custom.cpp:4078-4090). Si el A/B muestra una
/// línea de latencia, escribir también la POS/CTL del primer bloque, como `arm_object`.
template <class Sched>
static void arm_dma_chain(Sched& sched, u16 arm_line, u8 channel,
                          eng::ChipView<eng::SpriteTag> chain);
```

**Integración:** en `emit_placements_into`, los canales cuyo selector vote `Chain` se emiten con `arm_dma_chain` y **se saltan** sus rearmes por placement (decisión por canal, §3.2.4); el resto sigue con el camino actual (primera config en `arm_line` + `arm_object` en cada `vstart`).

#### 3.2.3 Cadena persistente + parcheo de POS/CTL (mejora propia)

Cuando el **conjunto de ranuras por canal es estable** (pool de tiros con `ranura → canal` fija en el nivel), la cadena se construye **una vez** (setup/nivel) y por frame solo se reescriben las 4 bytes de `POS/CTL` de cada bloque:

```cpp
/// Parchea POS/CTL del bloque `i` (4 bytes) sin tocar la DATA. Llamar tras el último
/// bloque mostrado (VBlank/borde inferior) o con doble cadena; el walk ocioso ya está
/// en el runway de ceros y no consume los bloques hasta que se recarga SPRxPT.
inline void patch_sprite_chain_block(SpriteDmaChain& chain, u8 block, u16 hpos, u16 vstart,
                                     u16 height, bool attach) noexcept;
```

**Condiciones de uso (documentar en la cabecera):** el orden por Y de los bloques no cambia entre frames (si dos objetos se cruzan, la POS del bloque inferior pasaría por encima del superior: reordenar los control words ese frame o caer a `Chain` reconstruida); alturas constantes; imágenes constantes. Es el caso exacto de las ristras de tiros (misma imagen, X fija, Y avanza, orden estable por antigüedad).

**Coste comparado por frame (por canal):**

| Estrategia | COPPER (words) | CPU (aprox.) | Cuándo |
|---|---|---|---|
| `Rearm` (actual) | por reaparición: 1 WAIT (2) + 4 MOVEs (8) ≈ 10 words | 0 | por defecto; imágenes grandes; pocas reapariciones |
| `Chain` (reconstruida) | 1 WAIT + 2 MOVEs ≈ 4 words | copiar `Σ(2 + h·2·w_words)` words + contadores | ≥2 bloques y datos pequeños/copiar sale a cuenta frente a los MOVEs ahorrados |
| `ChainPersistente` | 1 WAIT + 2 MOVEs ≈ 4 words | 4 bytes por bloque | ranuras estables (ristras, formaciones) |

La decisión fina se cierra con la medida (bus y ciclos) en la demo A/B; los umbrales quedan **documentados en el selector** con el número medido, no cableados a ojo.

#### 3.2.4 Selector de estrategia por canal

```cpp
// Decisión por canal, una vez por frame, sobre los placements ya ordenados (sin heap):
//  - Rearm:            nº de bloques del canal < 2, o datos grandes (words > umbral medido), o cadena no cabe.
//  - Chain:            ≥2 bloques, cadena cabe en la storage del llamador, y la copia de datos cabe en el presupuesto.
//  - ChainPersistente: el pool declara ranuras estables este frame (flag del juego) y la storage ya está construida.
enum class SpriteEmitStrategy : eng::u8 { Rearm, Chain, ChainPersistent };
struct SpriteEmitPlan { SpriteEmitStrategy strategy[kSpriteChannels]; };
[[nodiscard]] SpriteEmitPlan choose_sprite_emit_plan(eng::Span<const HwSpritePlacement> placements,
                                                     eng::Span<const SpriteDmaChain> chains) noexcept;
```

**Doble buffer:** las cadenas van dobles (2 `SpriteDmaChain` por canal, alternadas por frame) cuando se reconstruyen mientras la anterior se muestra; con `ChainPersistente` basta una con el parcheo en el VBlank.

**Tests host:** golden de la secuencia de words (cadena de 2–3 bloques byte a byte); gap/solape → `false`; capacidad insuficiente → `false` y `used` sin tocar; `attach` en CTL del bloque impar; offsets de `block_pos_words`; `patch_sprite_chain_block` cambia solo las 4 bytes; selector con canales mixtos.

**Demo (A/B):** ristra de ~32-40 tiros verticales servida por **1 canal**: `Rearm` vs `Chain` vs `ChainPersistente`; métricas: fps/ciclos (`measure-fps.mjs`), words de copperlist, perfil; visión con secuencia (todos los tiros presentes, sin columnas fantasma, sin flicker). El fallo típico a vigilar es la **columna fantasma** (`sprite-dma.md`): storage a cero, orden ascendente y PT recargado por frame son las tres condiciones.

**Criterio de salida:** HOST en verde; A/B medido y registrado (`VALIDATION.md` de la demo); la estrategia por defecto sigue siendo `Rearm` hasta que la medida demuestre la ganancia en el caso concreto.

### 3.3 F4.2 — Bending por tabla de seno (`SPRxPOS` por línea)

**Objetivo:** driver de deformación horizontal de un sprite que abarca N líneas, moviendo su X por línea con una tabla precalculada (agua, ondas, distorsión).

**Mecanismo:** escribir `SPRxPOS` (nueva X) y `SPRxCTL` (**`VSTART = línea actual`**, `VSTOP = base + altura`) al inicio de cada línea rearma el canal y la fila siguiente se dibuja en la X nueva (`custom.cpp:4078-4090`); el walk del DMA aporta una fila de DATA por rearme (`custom.cpp:12012-12023`). Es la misma familia de carrera contra el haz que Jim Power (215): **no dar la técnica por buena sin medida**.

**Contrato (driver, no intent):**

```cpp
// eng/graphics/drivers/sprite_bend.hpp (propuesto)
/// Emite, para `line` en [base_y, base_y + height), un WAIT + MOVE SPRxPOS + MOVE SPRxCTL
/// con x = x_base + offsets[(phase + line) & mask]. `offsets` es una tabla precalculada
/// (SineTable/`kPathSine` o tabla cocinada); `step` permite aplicar 1 valor cada 2 líneas
/// (mitad de coste, deformación más gruesa).
template <class Sched>
void emit_sprite_bend_into(Sched& sched, u8 channel, eng::ChipView<eng::SpriteTag> data,
                           u16 base_y, u16 height, u16 x_base,
                           eng::Span<const s16> offsets, u8 phase, u8 step = 1);
```

**Coste:** 1 WAIT + 2 MOVEs por línea ≈ 6 words de copperlist por línea (rearme vertical puro); con `step = 2`, la mitad. Presupuesto de banda por línea según `BUS_BUDGET.md`; si el Copper se adelanta al haz (síntoma de 215), aplicar la receta de pacing de esa investigación o reducir a `step > 1`.

**Tests/demo:** HOST del generador de offsets (amplitud/fase/step, reutilizando `kPathSine`); demo de un sprite alto con onda; visión por secuencia (onda suave, sin filas saltadas) + cadencia.

**Criterio de salida:** demo validada con visión y sin picos; documentar en la ficha de técnica (`sprite-horizontal-multiplex.md` §bending o nueva ficha) con la cita de fuente.

### 3.4 F5 — Márgenes automáticos de FastCopy

**Objetivo:** que el juego no calcule a mano el padding de los BOBs rápidos en dual playfield; el asset declara desplazamiento máximo por eje y **el pipeline host** genera el bitmap con márgenes y el tamaño final; `FastBobLayer` (ya existente) sigue decidiendo copia vs degradación.

**Contrato (datos de asset, no del runtime):**

```text
  preproceso (host):  sprite .raw + max_step_x/max_step_y (+ doble buffer²)
      left  = max_step_x_negativo (máx. desplazamiento a la izquierda)
      right = max_step_x_positivo
      top   = max_step_y_negativo ; bottom = max_step_y_positivo
      (los cuatro doblados si el juego usa doble buffer)
      → hoja padding' = (w + left + right) x (h + top + bottom), con la imagen centrada
      → el BOB se declara con el ancla en (left, top): el juego sigue usando su x/y
```

**Ubicación:** el cálculo vive en el pipeline de cocina de sprites (host); en el engine solo se documenta el contrato del asset (comentario en `Bob`/`FastBobLayer`). Ningún campo nuevo en `Visual`/`CompositePart` (frontera §2.4).

**Tests/demo:** HOST del cálculo de márgenes (extremos, doble buffer, redondeo a words); demo DPF con BOBs rápidos dibujados con `FastBobLayer` (HOST-355) y visión (sin rastros, sin huecos).

### 3.5 F7 — Integración de la paleta de sprites y F4.3 (palette splitting)

**Objetivo:** cerrar el planificador `sprite_palette.hpp` (hecho, HOST-433) con su integración real y una demo de división vertical de colores.

**Instrucciones:**

1. En el compositor de sprites, tras asignar canales: rellenar `SpritePaletteUse` por placement (canal asignado, `first/count` de registros usados, franja `top/bottom`, puntero de identidad de la paleta, `z`) y llamar a `plan_sprite_palettes(uses, decisions, copper_out)`.
2. Propagar las decisiones: `CopperSwitch` añade sus `CopperIntent(PaletteLine)` al plan (ya lo genera el planificador); `Degrade` marca la parte/actor a BOB (misma degradación que `as_bob`, sin ruta nueva).
3. Extender `HwSpriteTemplateView`/actores: los colores de franja (`HwSpritePaletteSwitch`) ya viajan; la integración solo necesita el caso «dos sprites de paletas distintas en el mismo par/franja» → reasignar canal si hay hueco, si no degradar por `z`.
4. Demo: un plano o jefe con paleta A en la mitad superior y B en la inferior (misma DATA, `CopperSwitch` en la línea de cambio) + un objeto de paleta en conflicto que degrada a BOB; visión por secuencia.

**Criterio de salida:** HOST de integración (uso → decisión → intent) + demo con gate visual; el estado pasa a la sección viva de `ROADMAP_UNIFICADO.md`.

### 3.6 F6 — Mecanismos de escenario: cuerdas/puentes, muelles y catapulta

**Objetivo:** dinámica barata de elementos flexibles y articulados, encajada con F1/F2. Puertas/destructibles/sólidos **no** se reimplementan (diseño y primitivas existentes, §2.7).

**Contrato (`eng/scene/rope.hpp`, propuesto; genérico sobre el escalar):**

```cpp
template <typename S> struct RopePoint { eng::math::Vec<2, S> p {}, prev {}; bool pinned = false; };
template <typename S> struct Rope {
    eng::Span<RopePoint<S>> points {};
    S rest_length {};   // distancia objetivo entre puntos (px)
    S gravity {};       // px/tick²
    u8 iterations = 2;  // pasadas de restricción (1-3)
    u8 damping = 240;   // 0..256 (256 = sin fricción)
};
template <typename S> void rope_step(Rope<S>&) noexcept;                  // verlet + restricciones
template <typename S> void rope_apply_impulse(Rope<S>&, u8 index, eng::math::Vec<2, S>) noexcept;

/// Contacto jugador ↔ cuerda: proyección del pie sobre cada segmento y adhesión.
struct RopeContact { bool hit = false; u8 segment = 0; s16 surface_y = 0; };
[[nodiscard]] RopeContact rope_find_contact(const Rope<s16>&, s16 feet_x, s16 feet_y,
                                            s16 half_width, s16 max_snap = 6) noexcept;
void rope_resolve_player(Rope<s16>&, s16& feet_x, s16& feet_y, s16& vy, const RopeContact&,
                         u8 bounce = 0) noexcept;
```

**Decisiones de coste/algoritmo:**

- Restricción de distancia con `eng::math::isqrt` (determinista, HOST-verificable); la variante sin `sqrt` del hilo (`diff·dx/(4·dist²)`) solo entra si un HOST de estabilidad demuestra que no oscila (oscila con `dist² ≫ rest²`).
- Damping en coma fija (`v*240/256` = multiplicación por constante + desplazamiento), sin división por variable; toda la simulación por ticks de juego.
- **Congelar** la cuerda cuando no hay contacto (el puente solo simula al pisarlo): el estado `pinned`/`awake` del llamador decide si `rope_step` corre.
- Emisión: un `CompositeVisual` de 1 parte por eslabón (o del punto medio del segmento para tablones) + `composite_emit_bobs`; en DPF, `FastBobLayer` con márgenes (F5).

**Muelles y catapulta (mismo archivo o `scene/mechanisms.hpp`):**

- `Spring`: compresión por `vy` del pie, tope `max_compress`, impulso de retorno `boost` + histórico de compresión; retorno amortiguado.
- `Catapult` articulada: estado `angle/angle_speed/loaded/firing`; secuencia del compuesto elegida por rango de ángulo (`composite_set_sequence`); **tip** `(x + sine(phase+64)·arm, y − sine(phase)·arm)` con `kPathSine`; el proyectil se suelta al superar el ángulo de disparo y se crea en el `EntityPool` con trayectoria balística cocinada.

**Tests host:** verlet determinista (mismos ticks → misma posición); extremos fijados; longitud estable tras N pasos; impulso desde arriba empuja hacia abajo y reparte; rebote 0 vs >0; catapulta dispara una sola vez y el tip coincide con la LUT.

**Demo:** puente de 12 eslabones cruzado por el jugador (deformación + apoyo) + catapulta que lanza una caja a un destructible; visión por secuencia y cadencia.

### 3.7 F4.1 — Free Form: cierre por la campaña 218

**Objetivo:** dejar de tratar el Free Form como pendiente del driver general y cerrarlo con la **campaña medida** en curso (`ROADMAP_218_CAMPO_UNICO.md`), que ya tiene el efecto, la instrumentación y el registro de intentos.

**Instrucciones:**

1. La fachada (`effects::FreeFormSpriteLayer`, HOST-419) y el fondo por ventana (`SpriteChannelLedger`/`plan_sprite_windows`) son el camino del engine; la demo 212 rota se sustituye por lo que cierre la 218 (no se resucita el driver viejo sin motivo).
2. Variante «data-driven» de la consulta (4 copperlists, actualización repartida en frames, swap): **solo** como diseño de la 218 si la medida lo admite; la nota útil que se incorpora es organizar la copperlist **por columnas contiguas** (un blit = una columna completa de altura) para minimizar el trabajo del Blitter en cada actualización.
3. Doble copperlist del Free Form (mejora apuntada en 213): siguiente paso natural tras el cierre de la 218.

**Criterio de salida:** el definido por la campaña 218 (fps/cadencia medidos, visión OK, registro en su bitácora); este documento solo enlaza.

### 3.8 F4.4 — Jim Power DATA-por-línea (condicionado)

**Estado:** demo 215 NO VERIFICADA; pacing abierto (adaptarse a la consulta de seguimiento ya enviada). El hilo de la consulta no aporta la pieza que falta (por qué el Copper se adelanta al haz con 4 planos y por qué la copperlist real usa `hpos` variable por línea).

**Instrucciones:** mantener la investigación (`consulta-jim-power-data-line-pacing-en.md`); al incorporar la respuesta, revisar `../WinUAE-DBG/` (fetch de sprites y robo de bus) y aplicar el protocolo A/B; fallback documentado: `POS+DATB+DATA` por columna (213). No marcar VERIFICADA sin visión + cadencia.

### 3.9 Cierre de microtareas de sprites (F4.5)

1. **Orden Y incremental** del productor de intents (reutilizar el orden del frame anterior con insertion/gnome sobre lista casi ordenada): pendiente del `ROADMAP_UNIFICADO.md`; HOST de estabilidad del orden + demo 217 sin regresión.
2. **Demos de cierre:** HUD con `SpriteLineLayer` (HOST-418 sin demo) y prioridad `BPLCON2` por franjas (intent `Priority` sin demo).
3. **Decidir 087** (arreglar o retirar): la técnica ya está cubierta por 207/208.

## 4. Orden de trabajo (por dependencia y valor)

| Corte | Trabajo | Valor | Prerrequisitos |
|---|---|---|---|
| **C0** (rápido) | F2.1 generadores V; orden Y incremental; F5 preproceso de márgenes | Cierra huecos de F2/F5 con coste bajo | Ninguno |
| **C1** (núcleo) | **F3** completo: builder + armado + selector (+ persistente) + demo A/B | Menos Copper y WAITs por objeto; base de las ristras | Fuente citada (§3.2, ya hecha); storage Chip del llamador |
| **C2** (efectos) | F4.2 bending; F7 integración + demo (F4.3) | Efectos de agua/ondas y color por zona con el arbitraje ya hecho | C1 no bloquea; scheduler/`BUS_BUDGET` |
| **C3** (juego) | F6 cuerdas/muelles/catapulta | Mecanismos jugables reutilizando F1/F2 | F1/F2 (hechos) |
| **C4** (condicionado) | F4.1 (cierre 218), F4.4 (pacing 215), demos de cierre F4.5 | Cierre técnico | Medidas/campaña en curso |

F3 **no** bloquea a C2/C3: el rearme por Copper actual es correcto; la cadena es optimización y se adopta por canal solo cuando la medida la respalde.

## 5. Protocolo de verificación (gate común)

1. **Host primero**: contrato puro + test determinista (y con dos escalares si la cabecera es genérica, AGENTS §1.11).
2. **Demo que consuma la fachada** (`eng/api`), sin registros ni tipos del backend en la lógica (AGENTS §1.13).
3. **Visión**: secuencia + Ollama sobre el efecto real (no captura única), con prompts por escenario (`DEMO_VISUAL_DEBUG.md`).
4. **Cadencia y coste**: `measure-fps.mjs` + perfil; sin tirones ni picos; comparación A/B cuando se sustituya un camino (F3, F4.2).
5. **Flicker**: `tools/test-regression.sh --flicker` en las demos comparadas.
6. **Fuente del hardware**: hallazgos citados `fichero:línea` en código y commit (F3/F4.1/F4.2/F4.4).
7. **Documentación**: ficha de técnica o contrato en `docs/`, `VALIDATION.md` de la demo, y estado en `ROADMAP_UNIFICADO.md` (única lista viva).

## 6. Criterios de aceptación (idénticos a `ROADMAP_JUEGO_SPRITES_BOBS.md` §7)

1. Contrato en el engine con test host determinista.
2. Demo con gate visual (secuencia + Ollama) y cadencia verificada.
3. `tools/test-regression.sh` en verde y sin flicker en lo comparado.
4. Documentación en su sitio canónico y sin duplicar tablas de estado.

## 7. Referencias

- Estado: `ROADMAP_UNIFICADO.md` §«Sprites hardware — estado»; campaña `ROADMAP_218_CAMPO_UNICO.md`; `BUS_BUDGET.md`.
- Fases y frontera contenido/materialización: `ROADMAP_JUEGO_SPRITES_BOBS.md` (F1–F8; §4.5).
- Contratos: `OBJECT_SYSTEM.md`, `SPRITE_CHANNEL_WINDOWS.md`, `VISUAL_EFFECT_SPRITE_DESIGN.md`, `PIXEL_ART_2D_ISOMETRIC.md` §5–§7.
- Técnicas: `sprite-layer.md`, `sprite-horizontal-multiplex.md`, `sprite-techniques-catalog.md`, `sprite-tricks-games.md`, `sprite-multiplexer-bob-fallback.md`, `dual-playfield-fastbobs.md`.
- Hardware/emulador: `winuae/sprite-dma.md`, `winuae/sprite-color-priority.md`; AHRM 3.ª cap. 4 y cap. 7; fuente `../WinUAE-DBG/custom.cpp` (`sprstartstop`, `SPRxCTL_DMA`, `generate_sprites`, fetch/avance de `s->pt`).
- Código del engine: `graphics/{sprite_allocator,sprite_manager,sprite,sprite_attached,sprite_palette,sprite_limits,sprite_channel_window}.hpp`, `scene/{composite_actor,trajectory,formation,entity_pool}.hpp`, `core/math/{sinetable,isqrt,spline}.hpp`, `core/util/collision.hpp`.
