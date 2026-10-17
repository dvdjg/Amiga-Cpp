# Roadmap: capa RPG/CRPG (`eng::rpg`)

Plan por hitos de la capa de juegos de rol sobre el engine. El diseño vigente está en [`RPG_LAYER.md`](../../engine/architecture/RPG_LAYER.md); este documento fija **qué se entrega, con qué dependencias y cómo se verifica** cada hito. La regla de cierre es la del repositorio: tests host por API y **una demo/juego que la ejerza** (el vertical slice de M6) antes de declarar la capa VERIFICADA.

Principio rector: un solo juego vertical (aldea + bosque + una mazmorra + 2–3 quests cruzadas) valida el núcleo mejor que quince sistemas a medias. Los hitos se ordenan para que cada uno sea jugable y medible; nada se entrega «a medias y sin consumidor».

## Estado

| Hito | Contenido | Estado |
|---|---|---|
| **M0 — Núcleo de mundo** | Hechos, objetos con instancia, inventario, equipo, stats derivadas, reloj, save | **HECHO** (HOST-454…457) |
| **M1 — Interacción** | Entidades de mundo, verbos, suelo/contenedores, enlace con `Needs` | **HECHO** (HOST-458/459; demo 001) |
| **M2 — Progreso de objetos** | Recetas/estaciones, mejora, encantamiento, durabilidad | **HECHO** (HOST-460/461; demo 001) |
| **M3 — Conflicto** | Vitality, pipeline de daño con `Rules`, loot, muerte → hechos | Pendiente |
| **M4 — Sociedad y economía** | Tienda, precios, robo, reputación, party | Pendiente |
| **M5 — Narrativa** | Quests declarativas sobre hechos, diálogo, journal, save del runtime | Pendiente |
| **M6 — Vertical slice** | Juego piloto en `games/` que cierra la verificación por demo | Pendiente |
| **M7 — Director y pulido** | Actos/gates de contenido, UI completa (journal/party/montura) | Pendiente |

---

## M0 — Núcleo de mundo (HECHO)

**Entregado**: `engine/include/eng/rpg/{types,facts,clock,items,inventory,equipment,stats,save}.hpp`. Decisiones y semántica: [`RPG_LAYER.md`](../../engine/architecture/RPG_LAYER.md) §4.

- Hechos persistentes con cambio efectivo (`epoch`/`changed`), `FactSet` de entidad.
- Objetos tipo+instancia con handles de generación, pool de capacidad fija, apilado.
- Bolsa, ranuras de equipo con etiqueta exigida como dato, recolección de `StatMod`.
- Stats derivadas por fases fijas con aritmética nativa del 68000.
- Reloj de mundo (día/hora/minuto) y save determinista por componente con versión.

**Verificación**: HOST-454 (kernel), HOST-455 (items), HOST-456 (inventario/equipo/stats), HOST-457 (save). Ejecutar: `bash tools/run-host-tests.sh --category rpg`.

## M1 — Interacción

**Estado: HECHO (2026-10-17).** Entregado: `eng/rpg/world.hpp` (entidades por composición, `GroundItems`), `eng/rpg/interaction.hpp` (cerraduras por etiqueta+metadato, `pick_up` con división de pilas, `drop_item` sin pérdidas), `eng/rpg/sim_bridge.hpp` (`eat_item`/ `rest_with_item` sobre `eng::sim::Needs`) y los verbos sobre el inventario y las cerraduras. Verificación: HOST-458/459 y la demo `demos/features/rpg/amiga/001_rpg_dungeon` (recoger, abrir el cofre, comer y abrir la puerta con llave, 48 fps, visión OK).

**Objetivo**: el jugador existe en el mundo y puede tocar cosas.

**Entregables**:

1. `eng/rpg/world.hpp` (o `entity.hpp`): `EntityId` (u16) + `WorldEntity` mínimo por composición — `kind` (Actor, Container, Scenery, Trigger, Mount…) y componentes como listas pequeñas (`Transform`/posición en mapa, `Interact` con verbos disponibles, `Container` que reutiliza `ItemBag`, `Lockable` con `key`+`FactSet` de permiso, `Ownable` con dueño). Sin jerarquía de clases.
2. **Suelo y contenedores**: lista de instancias con posición (`GroundItem { ItemHandle, position }` por región), `pick_up`/`drop` como funciones que mueven handles entre `ItemBag` y suelo; cofres = `ItemBag` de entidad.
3. **Verbos** sobre `eng::util::IntentQueue`: `Take`, `Drop`, `Use`, `Equip`, `Talk` (stub), `Open`. Cada verbo: valida → muta → emite hecho.
4. **Uso y consumo**: `UseSystem` sobre `ItemDef.use_effect` (id de efecto del juego: comer, beber, leer, llave); comer/beber llama a `eng::sim::feed` sobre la criatura del jugador (bridge M1, ver `SIM_ECOSYSTEM.md`); llave abre `Lockable` y escribe hecho `door_opened` con metadata (puerta).
5. **Durabilidad**: desgaste por uso con reglas del juego (tabla de desgaste por `use_effect` y por golpe futuro); rotura → destrucción + hecho.

**Dependencias**: `eng::sim` (criatura del jugador, `Needs`), `eng::util::IntentQueue`, mapa/celdas del juego (`eng::field`/`WORLD_FORMAT.md`).

**Verificación**: tests host nuevos (interacción: pickup/drop/use/equip contra un mundo de juguete) + demo interactiva mínima (mover, coger, usar) en `demos/features/rpg/amiga/` con HUD 1bpp y validación de visión.

## M2 — Progreso de objetos

**Estado: HECHO (2026-10-17).** Entregado: `eng/rpg/craft.hpp` (recetas por capacidad o tipo, estación por etiquetas, habilidad, transaccional), `eng/rpg/affix.hpp` (`AffixDef`, `apply_affix`, `gather_item_mods` con escala de mejora, `reinforce`) y `gather_equipment_mods` extendido (afijos + mejora). Verificación: HOST-460/461 y la demo 001 (forja la espada: +3 base +2 afijo, +20 % por dos niveles → ATAQUE 14 visible en el HUD). La **durabilidad por uso** queda como regla de juego sobre `ItemInstance::durability` (el dato y el desgaste por crafteo existen; el consumo por golpe llega con M3).

**Objetivo**: el equipo progresa (craft, mejora, encantamiento) y el desgaste importa.

**Entregables**:

1. `Recipe` + `CraftingSystem`: estaciones por etiqueta (`station_tag`), inputs por tipo o etiqueta, `skill_req`, `tools_tags`, outputs; valida contra `ItemBag`+`Equipment`, consume, produce instancias (`outputs[{type, qty, flags}]`), emite `Crafted`.
2. **Mejora** (`Reinforce`): sube `upgrade` y consume materiales; **encantamiento** (`Enchant`): afijo nuevo en la máscara `affixes` con `AffixDef { tags_permitidos, StatMod[] }` y restricciones por etiqueta. Tabla de afijos = contenido.
3. `gather_equipment_mods` extendido para expandir también afijos y aplicar la escala de `upgrade` (fases del pipeline de stats documentadas en `RPG_LAYER.md` §4).
4. Skill de crafteo: primer uso de `eng::sim::Skills` para el actor (o tabla ligera propia si el juego no usa la capa de persona).

**Verificación**: tests host (recetas: falta material, estación incorrecta, éxito; afijo restringido) + extensión de la demo de M1 con un yunque y una receta.

## M3 — Conflicto

**Objetivo**: combate intercambiable sobre un único pipeline de daño.

**Entregables**:

1. `Vitality` (HP + pools de recurso opcionales) enlazada con `eng::sim::Needs` (daño → `injury`/`fear`); muerte por HP 0 → `Died` con causa.
2. `DamagePipeline` por fases fijas (payload → mods atacante → mods defensor → clamps → Vitality → efectos laterales) con resistencias por **etiqueta** (`item_tags`/`DamageTag` del juego).
3. Política `Rules` de plantilla (estilo Zelda tiempo real · turnos FF · táctico por celdas) + `Targeting` por política; el pipeline no cambia entre estilos. `CombatConfig { rules, death, friendly_fire }` por zona.
4. `LootTable` y entrega de recompensas: `Loot` escucha `Died` (nunca el combate entrega XP/objetos directamente); `RewardGrant { xp?, items[], gold?, set_facts[], reputation[] }` como embudo único de balance.
5. IA de combate de criaturas: `eng::sim` conductas `Hunt`/`Flee`/`Avenge` guiadas por agresores (aggro ligero), sin duplicar el planner.

**Verificación**: tests host (pipeline: orden de mods, resistencias, clampeo; dos rulesets sobre el mismo daño) + demo de combate realtime mínimo con medición de presupuesto (reglas de `METODO_OPTIMIZACION.md`).

## M4 — Sociedad y economía

**Objetivo**: tienda, robo, reputación y party.

**Entregables**:

1. `Merchant` (stock como `ItemBag`, moneda aceptada, horario con `WorldClock`) y precios `f(base, demanda de eng::sim::Economy, reputación, escasez)`; compra/venta como intenciones.
2. `TheftSystem`: `Steal`/`PickLock` con probabilidad y testigos; los testigos escriben `FactSet` (o hechos) y la facción ajusta reputación (`eng::sim::Society::adjust`); guardias reaccionan a hechos, no a la quest.
3. Reputación por testigo/facción con la matriz facción↔facción (extensión mínima de `Society`, documentada en la ficha de sim si procede).
4. `Party`: líder, miembros (`EntityId`), modo de botín, formación con `eng::scene`/steering; invitar/despedir como intenciones.

**Verificación**: tests host (precio con reputación, robo con testigo, party cambia botín) + demo: aldea con tendero y un guardia.

## M5 — Narrativa

**Objetivo**: quests interconectadas por hechos, diálogo declarativo y journal.

**Entregables**:

1. `QuestDef`/`QuestRuntime`: stages con `when: Condition` y `then: Action[]` (mínimo DSL de condiciones/acciones de `RPG_LAYER.md` §5.5); suscripción por hecho (`FactId` → bitset de quests) + salida rápida por `epoch()`; `on_complete`/`on_fail`.
2. Dos o tres quests **cruzadas en hechos** (la demo las exige: matar al lobo abre el comercio y cierra la caza).
3. **Diálogo**: nodos con `when` sobre hechos, opciones con acciones; el mismo reproductor sirve la tienda y las amenazas. Texto en fichero de contenido.
4. `Journal` (solo lectura del estado para el HUD) y serialización del `QuestRuntime` como bloque del save (versión propia).
5. Puente de recompensas: las quests emiten `RewardGrant` (mismo embudo que M3).

**Verificación**: tests host (condiciones/acciones, suscripción por hecho, quests cruzadas deterministas, save del runtime) + demo con las quests cruzadas y journal en el HUD.

## M6 — Vertical slice (verificación por demo)

**Objetivo**: cerrar la verificación por demo de toda la capa — «aldea + bosque + 1 mazmorra + 2–3 quests enlazadas».

**Entregables**:

1. Juego en `games/` (p. ej. `games/400_rpg_slice`) con: jugador con inventario/equipo, un tendero, una estación de craft, un enemigo, una mazmorra con llave, 2–3 quests que comparten hechos.
2. HUD de juego (inventario/diálogo/journal) con `eng::ui` o superficie propia; franja de versión/depuración según `AGENTS.md` §0.
3. `INTENT.md`/`VISION.md`/`VALIDATION.md` del juego; presupuesto de 50 fps medido y validado con visión (secuencias, no capturas suelas — `DEMO_VISUAL_DEBUG.md`).

**Verificación**: pipeline completo `build → run → analyze` + visión Ollama + medidas de fps/cadencia. Al terminar, **quitar la marca NO VERIFICADA** de las cabeceras de `eng/rpg/` y de `RPG_LAYER.md`.

## M7 — Director y pulido

**Objetivo**: pacing de historia y UI completa.

**Entregables**: `DirectorSystem` (actos/capítulos que habilitan packs de quests sin sustituir hechos), UI de journal/party/tienda completa con `eng::ui`, monturas y refugio con sus hechos (`MountLink`/`Shelter`), guardado/carga desde el juego (menú + E/S del mini-SO con `FileHandle`).

**Verificación**: tests host + el juego de M6 ampliado; medición de footprint por perfil (512 KB / 1 MB).

---

## Dependencias externas y riesgos

- **`eng::sim`**: M1/M3/M4 dependen de su criatura de jugador y conductas; si el juego no usa la sim (Zelda puro), el bridge se apaga por configuración (el kernel RPG no incluye `eng::sim`, no hay coste).
- **Mapa/posición**: M1 necesita un mapa con regiones/celdas; se reutiliza `eng::field` + `WORLD_FORMAT.md`, sin inventar otro.
- **Contenido**: las tablas (objetos, recetas, afijos, quests) se escriben como datos C++ en ROM al principio; mover a fichero cocinado en host (pipeline de assets) es una decisión de M2 que no cambia el kernel.
- **Presupuesto**: el kernel no está en el bucle de render; los motores que sí lo estén (combate realtime, diálogo) pasan por `METODO_OPTIMIZACION.md` antes de darse por buenos.
- **Riesgo de sobre-diseño**: todo hito exige su demo; no se implementan sistemas «por si acaso» sin consumidor (regla §1.7).

