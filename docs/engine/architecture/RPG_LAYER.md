# Capa RPG/CRPG del engine (`eng::rpg`)

Diseño de la capa que permite construir juegos de rol (estilo Ultima VII, Zelda o Final Fantasy) **sobre el engine existente**: un núcleo de mundo con datos estables y serializables (hechos, objetos con instancia, inventario, equipo, progreso, reloj) y **sistemas** que aplican reglas por **intención** sin acoplarse entre sí. La simulación de ecosistema (`eng::sim`) queda debajo como sustrato vivo: la simulación **propone** (una criatura tiene hambre), la capa RPG **interpreta** (debe comer o pierde vida).

Las piezas implementadas son el **núcleo M0** y los hitos **M1 (interacción)** y **M2 (progreso de objetos)** en `engine/include/eng/rpg/`, verificados con HOST-454…461 y con la demo `demos/features/rpg/amiga/001_rpg_dungeon` (build → run → analyze OK, 48 fps, visión local; medidas y límites en su `VALIDATION.md`). Los motores de conflicto, sociedad y narrativa están diseñados aquí y planificados en [`ROADMAP_RPG.md`](../../guides/roadmap/ROADMAP_RPG.md).

## 1. Principios (qué se copia y qué no)

El plan de partida (consulta externa) propone «núcleo de mundo (datos) + sistemas (reglas por intención) + motores (límites de dominio y tick)». Se adopta la **forma** y se adaptan estas decisiones al engine real:

- **No hay ECS genérico ni `RPGGame` omnisciente.** El núcleo son `struct` de datos con capacidad fija por plantilla y **handles con generación** (patrón ya usado en `TaskHandle`/`TimerHandle`); los sistemas son funciones libres sobre esos datos. Cero heap, cero virtuales, cero despacho dinámico.
- **`ICombatRules`/`IProgressionRules` no son interfaces virtuales**: son **políticas de plantilla** (`Rules`), como `Traits` en `eng::sim` o `Broadphase` en `eng::ai::steering::crowd`. El estilo de combate se decide en compilación (realtime/turnos/táctico), no con `if (style == FF)`.
- **Etiquetas, no jerarquías**: `ItemDef.tags` es una máscara `u16` (`weapon`, `key`, `material`…). Las reglas preguntan capacidades; nunca existe `class Sword : Weapon`.
- **Objeto = tipo + instancia**: `ItemDef` (catálogo, contenido en ROM) + `ItemInstance` (8 B: cantidad, durabilidad, mejora, afijos, metadatos). Sin instancia no hay mejora de armas ni encantamientos.
- **Hechos, no llamadas entre sistemas**: quests, diálogos y reglas sociales **leen y escriben el `FactStore`**; matar al dragón escribe `dragon_dead` y la puerta, el comerciante y la guardia reaccionan sin conocerse.
- **El save es por componente** desde el primer día (`ByteWriter`/`ByteReader`, formato determinista con versión); el contenido (catálogos, recetas, quests) nunca se serializa.

## 2. Posición en la arquitectura

```text ┌──────────────────────────────────────────────────────────────────────┐ │ Juego (contenido): tablas de objetos, recetas, quests, diálogos,     │ │ balance, mapas — datos, no código de motor                           │ ├──────────────────────────────────────────────────────────────────────┤ │ Capa RPG (`eng::rpg`)                                                 │ │  Motores (objetivo): progreso · conflicto · sociedad/economía ·      │ │                      narrativa  — políticas por plantilla            │ │  ─────────────────────────────────────────────────────────────────   │ │  Núcleo de mundo (M0, implementado): hechos · objetos/instancias ·   │ │  inventario · equipo · atributos derivados · reloj · save            │ ├──────────────────────────────────────────────────────────────────────┤ │ Simulación (`eng::sim`): criaturas, necesidades, mente, sociedad,    │ │ economía, clima, terreno, LOD — el mundo vivo                         │ ├──────────────────────────────────────────────────────────────────────┤ │ IA (`eng::ai`): GOAP/HTN, utilidad, navegación, steering, percepción │ ├──────────────────────────────────────────────────────────────────────┤ │ Engine: escena/objetos, tilemap/scroll, UI, audio, mini-SO, recursos │ └──────────────────────────────────────────────────────────────────────┘
```

| Capa | Qué aporta | Documento |
|---|---|---|
| Núcleo RPG | Estado de partida del jugador y del mundo jugable (M0) | este documento §4 |
| Motores RPG | Reglas por intención: craft, combate, comercio, quests | este documento §5 |
| Simulación | Criaturas con necesidades, sociedad, economía, clima | [SIM_ECOSYSTEM.md](SIM_ECOSYSTEM.md) |
| Persona NPC | Arquetipos, expresión, lectura de tells (capa social de la sim) | [NPC_PSYCHOLOGY.md](NPC_PSYCHOLOGY.md) |
| Planificación | GOAP/HTN para rutinas y objetivos de NPC | [GAME_AI_LIBRARY.md](GAME_AI_LIBRARY.md) |
| Presentación | Sprites/BOBs, playfields, HUD | [OBJECT_SYSTEM.md](OBJECT_SYSTEM.md), [PIXEL_ART_2D_ISOMETRIC.md](PIXEL_ART_2D_ISOMETRIC.md) |

## 3. Qué se reutiliza y qué no se duplica

Antes de crear cada pieza se buscó su equivalente en el engine (regla §1.7 de `AGENTS.md`):

| Necesidad RPG | Ya existe en | Decisión |
|---|---|---|
| Hambre, fatiga, miedo, exposición | `eng::sim::Needs` (+ `feed`/`rest`/`hurt`) | **Reutilizar**: los vitales del jugador enlazan con `Needs` (bridge en M1/M3); no se inventa un segundo modelo de necesidades |
| Sociedad, facciones, reputación | `eng::sim::Society` (reputación por facción), `Relationship` | **Reutilizar** para reputación de mundo; la capa RPG añade solo lo que falta (matriz facción↔facción, reputación por testigo) |
| Economía de bienes | `eng::sim::Economy` (precio por oferta/demanda), trueque | **Reutilizar**: los precios de tienda compondrán `base × demanda × reputación`; el trueque IA ya existe |
| Clima, estaciones, refugio | `eng::sim` (`Climate`, `Season`, conductas) | **Reutilizar**: horarios de tienda y rutinas leen el reloj RPG + clima |
| Movimiento y rutas de NPC | `eng::ai` (astar, steering), `eng::sim` (rutas macro, LOD) | **Reutilizar** sin cambios |
| Planificación de NPC (trabajos, rutinas) | `eng::sim::planner` (GOAP/HTN) sobre `eng::ai` | **Reutilizar**: una quest no es un plan GOAP, pero un NPC que «va a la taberna de noche» sí |
| Cola de intenciones | `eng::util::IntentQueue` (`core/util/intent_queue.hpp`) | **Reutilizar** en el motor de intenciones (M1): capacidad fija, sin heap |
| Inventario de criaturas (ecosistema) | `eng::sim::Inventory` (pilas `ItemKind` para la IA) | **Distinto a propósito**: el ecosistema clasifica recursos (comida/material/herramienta) para decidir; el RPG necesita instancias con identidad. El bridge proyecta `ItemDef.tags` → `ItemKind` cuando un objeto cae al mundo de la sim |
| «Hechos» del planificador | `eng::ai::Fact` (índice en `BitSet<32>` local y efímero) | **Distinto a propósito**: el `FactStore` es estado de partida persistente y compartido; ambos coexisten |
| Guardar/cargar | `ByteWriter`/`ByteReader` (`core/util/binary.hpp`), E/S del mini-SO | **Reutilizar**: el formato de `save.hpp` es un bloque más del pipeline de ficheros |
| UI (diálogos, inventario, journal) | `eng::ui`, `Surface`, `Font8` | **Reutilizar**: la presentación se suscribe a eventos del estado, no calcula reglas |

## 4. El núcleo de mundo (M0, implementado)

| Cabecera | Aporta | Huella típica |
|---|---|---|
| `eng/rpg/types.hpp` | Ids (`FactId`, `ItemTypeId`, `StatId`), centinelas y etiquetas `item_tags` | — |
| `eng/rpg/facts.hpp` | `FactStore<MaxFacts>` (hechos con cambio efectivo) y `FactSet<Max>` (hechos de entidad) | `FactStore<64>` ≈ 150 B |
| `eng/rpg/items.hpp` | `ItemDef`, `ItemInstance` (8 B), `ItemCatalog<MaxTypes>`, `ItemStore<MaxItems>` con `ItemHandle` de 2 B, apilado | `ItemStore<128>` ≈ 1.3 KB |
| `eng/rpg/inventory.hpp` | `ItemBag<Slots>` + `bag_add` (apilado), consultas por tipo/etiqueta, peso | `ItemBag<16>` ≈ 40 B |
| `eng/rpg/equipment.hpp` | `Equipment<Slots>` (ranura = índice con etiqueta exigida como dato), `equip_item`/`unequip_item`, `gather_equipment_mods` | `Equipment<8>` ≈ 32 B |
| `eng/rpg/stats.hpp` | `StatMod`, `StatSheet<MaxStats>`, `recompute_stats` (fases fijas) | `StatSheet<16>` ≈ 66 B |
| `eng/rpg/clock.hpp` | `WorldClock` (día/hora/minuto) con avance por división nativa | 4 B |
| `eng/rpg/save.hpp` | Serialización por componente, determinista y con versión | buffer ≈ 1.5 KB para el perfil de ejemplo |
| `eng/rpg/world.hpp` (M1) | `EntityId`, entidades por composición (kind + verbos + índice de componente) con ids monótonos, y `GroundItems` (objetos con posición) | entidad 10 B; entrada de suelo 6 B |
| `eng/rpg/interaction.hpp` (M1) | `LockSet` (llave = etiqueta + metadato), `pick_up` (división por `stack_max`), `drop_item` sin pérdidas | ≤ 3 B por cerradura |
| `eng/rpg/sim_bridge.hpp` (M1) | Único fichero que incluye `eng::sim`: comer/descansar alimentan `Needs` | — |
| `eng/rpg/craft.hpp` (M2) | `Recipe`/`Ingredient`/`RecipeBook` y `craft` transaccional (estación y habilidad como datos) | receta 8 B; ingrediente 4 B |
| `eng/rpg/affix.hpp` (M2) | `AffixDef`, `apply_affix`, `gather_item_mods` (afijos + escala de mejora) y `reinforce` | afijo 4 B |

Decisiones que blindan el núcleo:

- **Handles con generación** (`ItemHandle {slot, generation}`): reciclar un hueco al destruir sube la generación y **invalida los handles viejos**; una referencia muerta nunca apunta a otra instancia. Cargar partida restaura slot y generación exactos (`ItemStore::restore`), así el inventario y el equipo guardados siguen valiendo.
- **Cambio efectivo en hechos**: `epoch()` (contador global) permite salir en O(1) cuando nada cambió; `changed(id)` dice qué cambió. Un consumidor (quests en M5, UI) guarda su último epoch; reescribir el mismo valor no lo despierta.
- **Etiqueta exigida por ranura como dato** (`Equipment::required_tags`): el motor no tiene un `enum` de ranuras; cuántas hay y qué exige cada una es contenido.
- **Pipeline de atributos con fases fijas** (suma → porcentaje recortado a ±100 → recorte final a `s16`), aritmética con `mul_wide`/`div32`/`divu_w` (nativas del 68000, sin `float` ni libcalls de 32 bits).
- **Save determinista**: recorridos en orden ascendente (id de hecho, hueco de pool); dos saves del mismo estado son byte a byte iguales, y un save tras cargar reproduce el original. Sin `reinterpret_cast` ni `memcpy` de structs.

## 5. Sistemas objetivo (motores) y sus políticas

### 5.1 Intenciones y comandos

El input del jugador y la IA no mutan el mundo directamente: encolan **intenciones** que los sistemas validan (`eng::util::IntentQueue`, capacidad fija). El sistema valida, muta el núcleo y **emite hechos** que despiertan a los demás (quests, UI, audio).

```text Input / IA → IntentQueue → Sistema (valida) → muta FactStore/ItemStore/... → hechos (epoch) ↑                                   │ └──── UI/audio/quests se suscriben ─┘
```

El mismo `Craft` lo dispara el jugador, un tutorial o una quest: cambia el emisor, no el camino.

### 5.2 Progreso de objetos (M2)

Recetas y estaciones por **etiquetas y rangos de tabla** (contenido, no código):

```text Recipe { inputs[{type|tag, qty}], station_tag, skill_req, tools_tags, outputs[{type, qty, flags}] }
```

Mejora y encantamiento operan sobre la **instancia**: `ItemInstance.upgrade` y la máscara `affixes` (bit i = afijo i del juego; los `AffixDef` aportan su rango de `StatMod`, restringido por etiquetas). La durabilidad baja con el uso y las reglas del juego deciden la rotura — el motor solo presta el dato y los hechos.

### 5.3 Conflicto (M3)

Un solo **pipeline de daño** con fases ordenadas y estables, y varias **`Rules` de plantilla** (no virtuales):

```text Intent (Attack/UseSkill/Flee) → Rules.legaliza y ordena → DamageRequest → pipeline: 1) payload base (arma/skill) 2) mods del atacante (equipo, crítico) 3) mods del defensor (defensa, resistencia por etiqueta, esquiva) 4) clamps/immunidades 5) aplica a Vitality (sellado: solo el kernel muta) 6) efectos laterales (status) → hechos (Died, CombatEnded) → loot/quests escuchan, no el combate
```

`Vitality` enlaza con `eng::sim::Needs` (daño → `injury`, miedo → `fear`); el estilo (tiempo real Zelda, turnos FF, táctico por celdas) es una política `Rules` con `tick/downgrade` propio y el mismo pipeline por debajo. La muerte es un **hecho** (`Died`), nunca una llamada a la quest.

### 5.4 Sociedad y economía (M4)

Tienda = `Merchant` (stock, moneda aceptada) + composición de precio `f(base, demanda de Economy, reputación, escasez)`. Robo = validación legal distinta sobre la misma infraestructura de inventario (`Ownable`, testigos → `FactSet` del guardia → hechos policiales). Party = lista de `EntityId` con líder y modo de botín; la formación usa `eng::scene/ai` sin duplicar movimiento.

### 5.5 Narrativa (M5)

Quests **declarativas** sobre hechos, sin `if (quest == 3 && step == 2)`:

```text QuestDef { id, stages[{ id, when: Condition, then: Action[] }] } Condition: HasFact · HasItem · InRegion · IsDead · ReputationAtLeast · PartySize · and/or/not Action:    SetFact · GiveItem · RemoveItem · Spawn · StartTimer · OpenQuest · CloseQuest · PlayCue
```

El `QuestRuntime` reevalúa **solo** las quests activas suscritas a hechos que cambiaron (índice `FactId` → bitset de quests), y usa `epoch()` como salida rápida. El propio runtime se serializa por componente (bloque propio del save). El **Director** (actos/gates) y el **Journal** (solo lectura para el HUD) van encima, sin sustituir a los hechos.

### 5.6 Diálogo (M5)

Nodos con `when` sobre hechos y opciones que ejecutan acciones; el mismo sistema sirve para comercio («mostrar inventario») y para amenazas. El texto es contenido (fichero); el motor solo evalúa y ejecuta.

### 5.7 Pipeline de contenido (decisión)

**Decisión vigente: tablas C++ `constexpr` en ROM** para los hitos M0–M2 (ya aplicada: `ItemDef`, `Recipe`/`Ingredient`, `AffixDef` viven como arrays estáticos y los sistemas reciben `eng::Span` sobre ellos). Razones: el A500 de 512 KB no tiene presupuesto para I/O de contenido por objeto; las tablas en ROM son gratis y el codegen lo verifica (`c_rpg_*`).

**Límite y transición**: los sistemas RPG nunca poseen las tablas, solo las *ven* (`Span`), así que mover el contenido a un fichero cocinado en host (UAF-R/`.engz` con el mismo layout de structs, cargado como `ByteView` + `as_words`) **no cambia el kernel ni los sistemas**: cambia solo quién produce el `Span`. Cuándo: cuando el contenido (ítems, recetas, afijos, quests) supere el presupuesto práctico de ROM o pase a ser editable por diseñadores — hito M4/M6; el pipeline host debe usar el mismo empaquetado que `WORLD_FORMAT.md`/`UAF_PACK.md`.

## 6. Orden de tick (objetivo)

```text
1. Reloj: advance(minutos del frame de juego)
2. Bridge de simulación: needs/schedules proponen (barato, LOD de eng::sim)
3. Drenar IntentQueue (jugador primero, luego IA)
4. Motor de progreso   (inventario, equipo, craft, uso)
5. Motor de sociedad   (comercio, robo, reputación, diálogo)
6. Motor de conflicto  (o posee el tick en modo turnos)
7. Motor narrativo     (quests/director reaccionan a los hechos del frame)
8. Limpiar bits de cambio; presentación (UI/audio) se suscribió en 4–7
```

El frame de **mundo** puede correr a menos frecuencia que el de render (la sim ya lo hace con LOD y ticks escalonados).

## 7. Enlace con la simulación existente

| Simulación (`eng::sim`) | Capa RPG |
|---|---|
| Hambre/fatiga/exposición (`Needs`) | Vitales y comida/posada: comer llama a `feed`, dormir a `rest` |
| Recursos nodales (forrajeo) | Nodos de recogida/cosecha que sueltan instancias del catálogo |
| Economía de bienes (`Economy`) | Precios de tienda y trueque con el jugador |
| Clima/hora | Horarios de tienda, refugio, encuentros; el reloj RPG alimenta `set_season`/rutinas |
| Path/agentes | Movimiento de NPC y monturas (sin duplicar navegación) |
| Sociedad (`Society`, `Pack`) | Reputación, guardias, grupos y party |
| Muerte por inanición (`check_death`) | Se convierte en hecho (`Died`) para quests/loot |

Regla de oro del puente: la simulación **propone**, el RPG **interpreta**. El bridge no duplica estado: lee del mundo de sim y escribe hechos/instancias.

## 8. Qué va en el engine y qué en cada juego

| Engine (`eng/rpg`) | Juego |
|---|---|
| Handles, pool de ítems, catálogo (tipo), inventario/equipo, stats, reloj, save | Tabla de tipos, balance y afijos |
| `FactStore` + condiciones/acciones (M5) | Definiciones de quests y diálogos |
| Sistemas genéricos (craft/tienda/quest/robo) | Recetas, facciones, historia |
| Pipeline de daño + puntos de política (`Rules`) | Combate concreto y UI |
| Serialización por componente | Mapas y contenido |

El engine **no** define «el combate de FF»: define `Vitality`, `ApplyDamage` y la política `Rules`. Un vertical slice (aldea + bosque + una mazmorra + 2–3 quests cruzadas) valida el núcleo mejor que quince sistemas a medias — hito M6.

## 9. Coste, memoria y presupuesto

- **Capacidades por plantilla** (`FactStore<64>`, `ItemStore<128>`, `ItemBag<16>`, `Equipment<8>`, `StatSheet<16>`): el perfil típico suma ≈ 1.6 KB de estado de mundo + ~130 B por actor (bolsa + equipo + stats). El A500 de 512 KB tiene margen; el contenido (catálogos, tablas) vive en ROM o en fichero.
- **Caminos de interacción, no de píxel**: los bucles del núcleo son O(pilas) u O(huecos ≤ 254) y corren al usar/coger/cargar partida, no por frame. Aun así no hay `*`/`/` de 32 bits: el reloj divide con `divmod32`/`divu_w` y los stats escalan con `mul_wide`/`div32`. Las reglas de [`METODO_OPTIMIZACION.md`](../../guides/optimization/METODO_OPTIMIZACION.md) aplican a los motores cuando lleguen (M3+): presupuesto de bus y mapa de contención antes de diseñar rutinas por frame.
- **Sin heap, sin virtuales, sin RTTI**: todo estado es POD con capacidad fija; las variaciones (estilo de combate, ranuras, reglas) son parámetros de plantilla o datos de contenido, nunca despacho dinámico.
- **Determinismo**: mismo estado → mismos bytes de save; el RNG del juego (`eng::Xoroshiro64pp`) es explícito y sembrable.

## 10. Verificación y estado

- **M0–M2 implementados** y verificados con tests host: HOST-454 (hechos/reloj), HOST-455 (objetos/pool/apilado), HOST-456 (inventario/equipo/stats), HOST-457 (save), HOST-458 (entidades/suelo), HOST-459 (cerraduras/pick-up/consume/puente sim), HOST-460 (crafteo), HOST-461 (afijos/mejora). Ejecutar: `bash tools/run-host-tests.sh --category rpg`.
- **Verificada por demo**: `demos/features/rpg/amiga/001_rpg_dungeon` ejerce M0–M2 en el 68000 (recoger, forjar, abrir cofre, comer con `Needs`, abrir con llave) a **48 fps**, con cuerda de vision local; medidas, ablaciones y límites declarados en su `VALIDATION.md`. Lección de coste de la campaña (jobs del plan vs streaming, HUD por texto): **DT-025**.
- **Probes de codegen m68k**: `c_rpg_*` en `tools/analyze/codegen-report.mjs` (sin libcalls de mul/div de 32 bits ni instrucciones 68020).
- Motores M3–M5 y vertical slice M6: diseñados aquí, sin implementar. Estado y orden en [`ROADMAP_RPG.md`](../../guides/roadmap/ROADMAP_RPG.md).

