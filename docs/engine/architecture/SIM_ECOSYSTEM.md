# Ecosistema simulado (`eng::sim`)

`engine/include/eng/sim/` modela un **mundo vivo** habitado por criaturas con
necesidades, personalidad, memoria afectiva, conocimiento, relaciones, jerarquía y
sociedad. El diseño sigue la filosofía de *Rain World*: no hay guiones ni enemigos "para el
jugador"; el comportamiento **emerge** de reglas simples (percepción + utilidad +
relaciones) que corren en un presupuesto mínimo. Es la capa de **simulación lógica**: no
conoce sprites, tiles, polígonos ni registros de hardware, y compila igual en host que en
el cruce `m68k`.

Esta familia es **distinta de `eng::ai`** ([GAME_AI_LIBRARY.md](GAME_AI_LIBRARY.md)):
`eng::ai` aporta los algoritmos genéricos (utilidad, GOAP, navegación, steering,
percepción); `eng::sim` aporta el **modelo de entidad viva** y reutiliza esas primitivas.
El eje `eng::board` ([BOARD_GAME_AI.md](BOARD_GAME_AI.md)) cubre la búsqueda por turnos.

La **capa de persona** ([NPC_PSYCHOLOGY.md](NPC_PSYCHOLOGY.md)) extiende `eng::sim` con
arquetipos, expresión no verbal, lectura de tells y evolución psicológica de partida; el
primer consumidor es `eng::cards` ([CARD_GAME_AI.md](CARD_GAME_AI.md)).

## 1. Principio: tres planos y un LOD de simulación

La viabilidad en un A500 (7 MHz, 512 KB–1 MB) no sale de un algoritmo aislado, sino de
**simular con detalle solo lo que se ve** y el resto de forma abstracta y escalonada.

```
  PLANO ABSTRACTO   todo el mundo            datos puros; tick cada `stagger_period`
        │                                    frames; sin percepción ni pathfinding
        │  realize_room / set_realized
        ▼
  PLANO REALIZADO   rooms cercanas a cámara  necesidades + trackers + utilidad;
        │                                    el juego añade física y pathfinding
        │  emitir representación
        ▼
  PLANO RENDER      2D / iso / 3D            sprites, BOB, playfield o polígonos
```

`SimWorld` implementa los dos primeros planos. El tercero vive fuera: la lógica de juego
no toca hardware (principio de [PUBLIC_API.md](PUBLIC_API.md)). La **proyección**
(2D, isométrico o 3D) entra como *policy* de plantilla en el render y en la percepción
espacial, nunca en el modelo de criatura, que usa siempre coordenadas de mundo
(`room` + `x,y`).

El paso de abstracto a realizado lo decide el **LOD alrededor del jugador** (`lod.hpp`):
`SimWorld::update_lod` marca **realized** cerca, **abstract** a media distancia y
**dormant** (congelado, sin coste) lejos; al acercarse el jugador, lo dormido se realiza
**gradualmente** (`lod_blend` sube por frames y `wake_per_frame` reparte cuántas despiertan,
evitando el "pop" y los picos de trabajo). El **aforo dinámico** (`season.hpp` + `biome.hpp`)
modula la capacidad de cada región por **estación** y **clima**. El **jugador simulado**
(`avatar.hpp`) usa los mismos sentidos y necesidades, y admite tanto IA (`player_step`) como
**entrada humana** (`player_control`), de modo que un humano sustituye a la IA sin tocar el
mundo.

## 2. Modelo de datos

Cada criatura es un agregado compacto, sin punteros ni heap, que vive en un array
contiguo. Las capacidades se fijan como **parámetros de plantilla** y el presupuesto se
valida en compilación (`static_assert`).

```cpp
eng::sim::AbstractCreature<MaxTrackers, MaxRelations> c; // arrays inline
```

| Tipo | Papel | Tamaño m68k |
|---|---|---|
| `EntityId` (`u16`) / `RoomId` (`u8`) | identidad y localización; centinelas | — |
| `Needs` | hambre, cansancio, miedo, social, herida y **exposición** + tipo de peligro | 7 B |
| `Personality` | diez rasgos `u8` `[0,100]` (supervivencia, conflicto, vínculo, trabajo) | 10 B |
| `Emotions` | doce afectos `u8` | 12 B |
| `Mind` | afectos + pulsiones (autonomía/deferencia) + memoria + agregados | 44 B |
| `Genome` | ocho genes `u8` (dotación hereditaria) | 8 B |
| `KnowledgeEntry` | creencia aprendida (tipo, sujeto, confianza) | 6 B |
| `Inventory` | hasta 4 pilas `(tipo, cantidad)` de objetos | 12 B |
| `Item` | objeto en el mundo (tipo, cantidad, room, posición, portador) | 12 B |
| `Senses` | sensibilidad y alcance por modalidad sensorial (+ ecolocalización) | 13 B |
| `Tracker` | entidad percibida, última posición, confianza, modalidades y saliencia | 14 B |
| `Relationship` | vínculo (`affinity`) y **carga emocional dirigida** (`affect`) | 6 B |
| `AbstractCreature<6,6>` | criatura completa | 308 B |
| `Climate<64>` | peligro ambiental por región | 128 B |
| `GroupMemory<8>` | memoria de conocimiento por facción | 416 B |
| `SimWorld<…>` | array de criaturas + grafo de rooms + clima + terreno + biomas + sociedad + objetos + planificador | 23 998 B |

Las **especies** (`Species`) son tablas inmutables que el juego declara una vez: dieta,
organización social (solitaria, manada, colmena, familia, territorial, **enjambre**), bits
de movimiento (andar, trepar, nadar, volar, saltar, cavar), capacidades y personalidad
base. Lo que no cambia no se copia por criatura. El movimiento es agnóstico de la
proyección: cada `Projection` traduce los bits a costes de tile.

Los **ids de entidad son monótonos y únicos**: `spawn` reutiliza el hueco de una criatura
muerta pero con un id **nuevo**, de modo que ninguna referencia antigua (tracker, relación)
apunta por accidente a la criatura reciclada; el mundo no crece indefinidamente y las
poblaciones pueden renovarse a lo largo de generaciones.

## 3. Necesidad ambiental genérica y representación del mundo

`exposure` representa el **estrés por el entorno** y va acompañado de un `HazardKind`
(lluvia, frío, calor, tormenta, polvo, radiación, inundación). El mundo fija tipo y
severidad; la criatura solo ve una presión que la empuja a refugio y una pista de
protección (`shelter_for`: techo, abrigo, sombra, recinto sellado, terreno elevado).

El **clima** (`climate.hpp`) es por región: `Climate<MaxRooms>` forma, consulta y disipa
peligros (`set`/`add`/`tick`), y `exposure_at` convierte severidad + **abrigo del terreno**
en la exposición efectiva (0 si la criatura está a cubierto). Con `diffuse_climate` el
peligro **se propaga como un frente** a las regiones vecinas (conservando el tipo) y se
disipa, de modo que una tormenta avanza por el mapa. Cierra el ciclo del refugio: el abrigo
nace del mundo, no de una estructura ad-hoc.

El mundo se representa de forma **algorítmica y agnóstica de la proyección**
(`terrain.hpp`): `TerrainKind` (suelo, abrupto, muro, agua, escalable, hueco, repisa,
peligro, cobertura) con coste, cobertura y abrigo, y `move_mask` (capacidades que lo
atraviesan). `TerrainMap<W,H>` expone `walkable`/`cost` para `eng::util::astar`, y las
regiones se clasifican con `RegionTerrain` (terreno dominante, abrigo, peligro). El render
2D/iso/3D solo **dibuja** esa semántica; el pathfinding y la decisión la **consultan**. El
terreno es **dinámico**: `apply_terrain_event` (inundación, incendio, derrumbe,
regeneración) lo cambia en caliente y, si procede, altera el clima de la región
(`TerrainEventParams`).

Los **biomas** (`biome.hpp`) unen las tres capas: un `BiomeKind` describe el terreno
dominante, el clima típico, los recursos y las especies; `species_fits_biome` comprueba que
la especie puede moverse por él y `SimWorld::apply_biome` vuelca el perfil en la región
(terreno, abrigo, peligro) y, si se pide, siembra su clima. Una ciénaga nace encharcada, una
montaña exige trepar y un desierto trae calor, sin duplicar terreno ni clima.

## 4. Decisión por utilidad

`behavior.hpp` implementa el ciclo de decisión por tick, heredado de los "módulos de
utilidad" de *Rain World*. Cada comportamiento calcula un score `[0,1000]` a partir de
necesidades, personalidad, trackers, conocimiento y afecto; gana el mayor, con
**histéresis**.

```
  Needs.tick → Trackers.decay → Knowledge.decay → Mind.update → score_behaviors → choose
                                                                    │                │
  Idle · Wander · Hunt · Flee · SeekFood · Sleep · Socialize ·   pesos por       ruido ± y
  SeekShelter · Tend · Help · Court · Submit · Defy · Teach ·     comportamiento   histéresis
  Forage · Avenge                                                   (BehaviorWeights)
```

- La puntuación base se compone con `eng::ai::Utility` (media ponderada entera,
  `muls.w`/`divs.w`), de modo que **no se duplica** el motor de utilidad.
- Los sesgos de personalidad y afecto se aplican con `apply_mod` (un único camino entero).
- `BehaviorWeights` reajusta la balanza por comportamiento sin tocar el algoritmo.
- `SimTraits` decide en compilación qué módulos existen (`emotions`, `society`,
  `knowledge`, `genetics`, `planning`); con `SimTraitsLean` se eliminan todos.

El GOAP de `eng::ai` **no** corre por frame: se reserva para planes ocasionales
(`planner.hpp`/`domain.hpp`, §9).

## 5. Percepción, afecto, conocimiento y memoria

- **Percepción multimodal** (`senses.hpp`): cada criatura tiene **visión** (cono frontal,
  alcance, cobertura), **oído** (omnidireccional, cruza regiones atenuado), **olfato**
  (corto, lo bloquea la cobertura), **tacto** y **gusto** (contacto) y **temperatura**. El
  mundo rellena `SenseTarget` (lo que cada candidato emite) y `perceive` produce
  `Observation`; sin trigonometría (sectores). La `salience` = fuerza + **novedad**.
- **Atención** (`focused`/`attention_score`): el estado interno **cambia lo que se percibe** —
  el miedo estrecha y acorta la vista pero agudiza el oído, la ira enfoca — y la decisión
  elige a qué atender por **confianza + saliencia + multimodalidad** (`best_attention_tracker`).
  Es el cierre percepción↔conducta. Los `Senses` se **expresan del genoma**
  (`senses_from_genome`): velocidad → vista fina y **ecolocalización**, sociabilidad → oído,
  tamaño → olfato.
- **Memoria de dos niveles** (`memory.hpp`): el **corto plazo** son los `Tracker`
  (multimodales, con saliencia, olvido rápido); el **largo plazo** es el `KnowledgeSet`
  (creencias persistentes, compartibles). La **consolidación** promueve a largo plazo lo
  que el corto plazo sostiene con fuerza o por recurrencia (`MemoryParams`), con mapeo de
  categorías (`Threat→Enemy`, `Den→Shelter`...) y sujeto entidad/**región**. La **memoria
  espacial** mantiene un **mapa mental** de regiones (refugio/peligro/comida) que el
  pathfinding macro usa: `preferred_refuge` y `route_room` (BFS de regiones) guían la
  migración off-screen; así el bucle por frame consulta solo el corto plazo y consolida
  cada `consolidation_period` ticks. El **mapa mental** resultante guía el movimiento tanto
  macro (`preferred_refuge`/`route_room`) como fino (`mental_map.hpp`: `MentalOverlay` para
  `eng::util::astar` y `deposit_mental_danger` para el `InfluenceMap`), y sus lugares se
  **olvidan** con el tiempo (`decay_places`).
- **Afecto** (`mind.hpp`): doce ejes que derivan por pasos hacia un objetivo calculado con
  necesidades, personalidad, recuerdos y sus propias realimentaciones. Dos **pulsiones**
  estables: `autonomy` (deseo de libertad) y `deference` (disposición a someterse). La
  fórmula es **paramétrica** (`AffectParams`).
- **Memoria episódica**: hasta `kMaxMemoryEvents` recuerdos con actor, intensidad y edad;
  hay recuerdos sociales (traición, sometimiento, cortejo, enseñanza, castigo...) que
  tiñen el afecto y permiten `attitude_toward(actor)` (rencor o afecto hacia alguien).
- **Afecto dirigido** (`relationship.hpp`): cada relación guarda `affinity` (vínculo
  estable) y `affect` (carga emocional que cambia rápido). `bond_score` combina ambos y
  los comportamientos eligen **a quién** ayudar (`Help`), cortejar (`Court`) o confrontar
  (`Avenge`, venganza selectiva). Es lo que da celos y rencor concretos, no agregados.
- **Conocimiento** (`knowledge.hpp`): creencias de largo plazo (fuentes de comida, refugio,
  peligros, identidades, herramientas, rutas) con confianza creciente. `learn` refuerza,
  `decay_knowledge` olvida y `share` **transmite** de una criatura a otra (madre a cría,
  explorador a manada): la base de una cultura de grupo. Parámetros en `LearningParams`.

## 6. Jerarquía y libertad

`hierarchy.hpp` da a las criaturas un sentido del rango sin estado global: `contest_power`
calcula el poder (dominancia, fuerza, estado), `should_submit` decide si conviene ceder
(con autonomía que encarece someterse, deferencia y miedo que lo abaratan) y
`submission_score`/`defiance_score` alimentan los comportamientos `Submit`/`Defy`. Todo
paramétrico (`HierarchyParams`): la misma maquinaria modela una manada de lobos, una
colonia de hormigas o una jerarquía de rivales.

## 7. Genética, colonias y ciclo de vida

- **Genética** (`genetics.hpp`): `Genome` de ocho genes; `inherit` combina dos progenitores
  con **sesgo de dominancia** y aplica mutación (`GeneticsParams`); `genome_to_personality`
  expresa el genoma en rasgos; `caste_of` clasifica castas (reina, obrera, soldado,
  zángano, exploradora, nodriza) con umbrales (`CasteParams`).
- **Colonias** (`colony.hpp`): censo de castas, reparto del rol más necesario según
  proporciones de diseño (`ColonyParams`), y **estigmergia** por feromonas que reutiliza
  `eng::ai::InfluenceMap` (depositar, decaer y seguir el rastro más fuerte). De ahí salen
  rutas de forrajeo que se refuerzan solas y reclutamiento hacia la comida, sin
  coordinador central, como en hormigas, abejas o termitas.
- **Ciclo de vida** (`lifecycle.hpp`): etapas por edad (cría, joven, adulto, anciano),
  madurez (`can_reproduce`), crecimiento, muerte natural y **reproducción**:
  `SimWorld::try_reproduce` engendra una cría de una pareja de vínculo suficiente con el
  genoma heredado (`newborn_genome`) y coste parental; `SimWorld::lay_brood` hace que una
  reina ponga la casta más necesaria, sesgando el genoma (`bias_for_caste`). Todo
  paramétrico (`LifecycleParams`).

## 8. Objetos, economía y representación

- **Inventario y objetos** (`inventory.hpp`/`object.hpp`): pilas de objetos con etiquetas
  (`inventory.hpp`) y objetos del mundo con posición y portador (`ItemStore`).
  `execute_domain_action` **materializa** los pasos del plan (`Forage`/`Eat`/`Gather`/
  `CraftTool`/`Build`): consume y produce recursos y deja estructuras en el mundo. Cierra el
  ciclo objeto↔GOAP: la criatura planifica para conseguir algo y luego lo consigue.
- **Economía** (`economy.hpp`): precio por `base + demanda`; la oferta abarata y el
  decaimiento devuelve a la base. `give_gift`/`offer_tribute` suben la reputación del
  receptor (`Society`) y la demanda del objeto. `execute_trade`/`offer_trade` cierran un
  **trueque** de objetos (con `bargain_score` por balance de valor y necesidad) que también
  mejora la reputación. Es el motor de las relaciones entre facciones.
- **Cuerpo procedural** (`body.hpp`): cadena de chunks con **IK FABRIK** genérica sobre el
  escalar (`double`/`q12`). `pose_from_behavior`/`pose_from_state` convierten conducta y
  afecto en **postura** (agacharse/retroceder por miedo, inclinarse por ira, menear la cola
  por alegría, tensarse ante un enemigo), de modo que la representación expresa el estado
  interno sin animaciones dibujadas a mano.
- **Rumores y memoria de grupo** (`rumor.hpp`): `GroupMemory` guarda un `KnowledgeSet` por
  facción; los individuos **contribuyen** con sus creencias (con distorsión) y la facción
  las recuerda. `SimWorld::diffuse_knowledge` difunde entre correligionarios de la misma
  región y `apply_group_knowledge` traduce la memoria colectiva en **reputación**
  (enemigos/aliados) y **demanda** económica. Es la cultura de grupo cruzando `Society` y
  `Economy`.
- **Lenguaje y gestos** (`communication.hpp`): una criatura **emite** una señal (llamada,
  alarma, amenaza, comida, cortejo, sumisión, saludo) derivada de su conducta y emoción, y
  las que la oyen en su región actualizan su memoria de corto plazo y su afecto
  (`apply_signal_effect`: la alarma da miedo, la sumisión **eleva al receptor**). El alcance
  depende del oído y de la ecolocalización. `SimWorld::broadcast_signals` lo ejecuta.
- **Cultura y rituales** (`culture.hpp`): la tradición es conocimiento (`KnowledgeKind::Ritual`)
  que se **hereda por enseñanza**; ante un evento (muere un aliado, se halla comida, aparece
  un enemigo) la criatura actúa el ritual que conoce (`ritual_for_event`/`perform_ritual`) y lo
  expresa con una señal (`SimWorld::enact_ritual`). Sin quien enseñe, la tradición se pierde.
- **Manadas** (`pack.hpp`): roles (líder, flanqueador, seguidor, explorador) y
  `coordinate_packs`: el líder con una presa percibida orienta a los miembros (relaciones
  `Pack`) a **envolver** el objetivo desde distintos flancos; es la coordinación táctica
  emergente guiada por señales y jerarquía.

## 9. Planificación ocasional (GOAP)

`planner.hpp` conecta `eng::ai::Goap` con la criatura sin reimplementar el planificador:
`plans(personality)` decide si la especie/criatura planifica (curiosidad + autonomía),
`PlannerDriver::replan` lanza un plan sobre el dominio del juego y `PlanRunner` ejecuta los
pasos por ticks. La mayoría de criaturas usa utilidad cada tick; unas pocas planifican para
objetivos de varios pasos (construir, conseguir una herramienta, llegar a un recurso
lejano). El planificador ocupa miles de bytes y se instancia en memoria estática o de fondo.

`domain.hpp` trae un **dominio de ejemplo** de objetos y construcción
(`Forage → Eat`, `Gather → CraftTool → Build`), con `SimInventory` (estado del agente),
`start_state` (inventario → hechos) y `SimActionKind` (paso del plan → acción ejecutable).
Es plantilla, no dogma: el juego define sus hechos y acciones.

`SimWorld` integra la planificación con `Traits::planning`: `replan(id, start, goal,
actions)` asigna un plan a una criatura y `current_action`/`advance_plan`/`abort_plan` lo
consumen; `planning_count` informa de los planes activos (hasta `MaxPlans`). El
`PlannerDriver` se guarda en un `PlannerHolder` vacío cuando `Traits::planning` es falso,
de modo que no ocupa RAM si no se usa (verificado por el gate de tamaños m68k).

El **dominio GOAP es un parámetro de plantilla** de `PlannerDriver` y de `SimWorld`
(último parámetro): por defecto el booleano ligero `SimGoap` (0 variables), o
`SimNumericGoap<N>` (hechos + `N` magnitudes: hambre, energía, miedo…) para objetivos con
umbrales numéricos. El algoritmo no cambia y el caso booleano conserva su footprint
(HOST-313).

La criatura puede planificar por **GOAP** (búsqueda A\*) o por **HTN** (descomposición): el
`HtnDriver` produce un `PlanRunner` que el mundo guarda con `store_plan` y consume igual
(`current_action`/`advance_plan`/`abort_plan`). La elección se declara con `PlanKind`
(`Goap` para un objetivo suelto, `Htn` para una tarea compuesta) y `plan_for` despacha
—decisión + presupuesto + replan en GOAP, o descomposición + `store_plan` en HTN—, de modo
que el juego no repite el `if`. Así una tarea compuesta (`build_shelter_htn`) convive con los
objetivos GOAP sin duplicar la ejecución (HOST-313/318).

En Amiga real/emulado, `demos/features/sim/amiga/001_sim_bench` mide el ecosistema completo
(`SimWorld` + planificación) con el reloj TOD de la CIA-A, separando tick de planificación:
A500 con 12 criaturas da **125 ticks/s** solo y **7 frames/s** con planificación (~18x más
caro planificar que simular), así que el cuello de botella es el planner. El mundo (~24 KB) va
en memoria estática (no cabe en la pila del 68000).

## 10. Cómo se reutiliza `eng::ai` (sin duplicar)

| Necesidad del ecosistema | Primitiva existente |
|---|---|
| Puntuar opciones | `ai/decision/utility.hpp` (`Utility`, `UtilitySelector`) |
| Recordar la última posición vista | `ai/perception/agent_memory.hpp` (los trackers lo amplían) |
| Candidatos de percepción | `util/broadphase.hpp` (`SpatialHash`) |
| Ruta realizada | `util/pathfinding.hpp`, `ai/navigation/{flow_field,waypoints,navmesh_lite}` |
| Movimiento continuo | `ai/steering/steering.hpp` (`seek`/`flee`/`arrive`, `pursue`/`evade`/`wander`/`avoid_circles`) |
| Peligro/cobertura difusa, feromonas | `ai/perception/influence_map.hpp` |
| Plan de varios pasos | `ai/planning/goap.hpp` (con caché), envuelto por `sim/planner.hpp` |

## 11. Ticks y presupuesto

`SimWorld` ofrece dos entradas, ambas sin heap y acotadas:

- `tick_realized(rng)`: por cada criatura realizada y viva —necesidades, entorno, olvido de
  trackers y conocimiento, mente y decisión—. Es el camino por frame; su codegen está
  gateado por `tools/analyze/codegen-report.mjs` (sin libcalls de 32 bits ni 68020).
- `tick_abstract(rng)`: reparte el mundo lejano. En cada llamada procesa solo las criaturas
  con `id % stagger_period == cursor`; avanza el cursor. Aplica necesidades, entorno,
  olvido rápido de trackers/conocimiento y **migración** por el grafo de habitaciones hacia
  el refugio cuando el entorno es severo.

## 12. Estado y verificación

| Cabecera | Contenido | Verificación |
|---|---|---|
| `types.hpp` | ids, `SimTraits`, saturación `u8`, `u8_scale`/`div_u16`, `trait_mod`/`apply_mod` | HOST-152…155 |
| `needs.hpp` | `Needs`, `HazardKind`/`ShelterKind`, tick y acciones | HOST-152 |
| `personality.hpp` | diez rasgos, `PersonalityTemplate::materialize` con jitter | HOST-152 |
| `species.hpp` | `Species`, `Diet`, `SocialStyle` (incl. `Swarm`), movimiento, `preys_on` | HOST-152 |
| `relationship.hpp` | `Relationship` (vínculo + afecto dirigido), `bond_score`, `most_loved`/`most_hated` | HOST-152 / HOST-154 |
| `tracker.hpp` | `Tracker`, `observe`/`decay`/`best_tracker`/`forget` | HOST-152 |
| `mind.hpp` | doce emociones, pulsiones, memoria, `update` paramétrico | HOST-152 / HOST-154 |
| `behavior.hpp` | dieciséis comportamientos, `score_behaviors`, `choose_behavior`, pesos | HOST-152 |
| `creature.hpp` | `AbstractCreature` (needs, mind, genome, knowledge...), flags | HOST-152 |
| `knowledge.hpp` | creencias, `learn`/`decay`/`share`, `LearningParams` | HOST-154 |
| `hierarchy.hpp` | poder, sumisión/rebeldía, `HierarchyParams` | HOST-154 |
| `genetics.hpp` | `Genome`, herencia/mutación, castas, `bias_for_caste`, `CasteParams` | HOST-154 |
| `colony.hpp` | `Colony`, `ColonyParams`, feromonas sobre `InfluenceMap` | HOST-154 |
| `lifecycle.hpp` | etapas, madurez, reproducción, `ReproState`, `LifecycleParams` | HOST-153 / HOST-154 |
| `inventory.hpp` | `Inventory`, `ItemKind`, etiquetas de objeto | HOST-157 |
| `object.hpp` | `ItemStore`, `execute_domain_action` (efecto material) | HOST-157 |
| `economy.hpp` | precios por oferta/demanda, regalos/tributos y **trueque** (reputación) | HOST-157 / HOST-168 |
| `terrain.hpp` | `TerrainKind`/`TerrainProfile`, `TerrainMap` (astar), `RegionTerrain`, **eventos de terreno** | HOST-158 / HOST-167 |
| `climate.hpp` | `Climate<MaxRooms>`, exposición efectiva por región | HOST-158 |
| `rumor.hpp` | `GroupMemory`, difusión y efectos sociales de los rumores | HOST-159 |
| `senses.hpp` | percepción multimodal, atención (`focused`) y sentidos por genoma | HOST-160 / HOST-162 |
| `memory.hpp` | corto plazo, consolidación y memoria espacial (`MemoryParams`) | HOST-161 / HOST-163 |
| `mental_map.hpp` | sesgo de lugares, overlay para `astar` e influencia de peligro | HOST-164 / HOST-166 |
| `biome.hpp` | biomas/ecosistemas: terreno + clima típico + especies por región | HOST-169 |
| `communication.hpp` | señales/gestos, recepción y efecto emocional/jerárquico | HOST-170 |
| `culture.hpp` | rituales, disparo por evento, herencia y actuación en el mundo | HOST-171 |
| `pack.hpp` | roles de manada, flanqueo y coordinación de caza | HOST-172 |
| `lod.hpp` | bandas de detalle por distancia (realized/abstract/dormant) con despertar gradual | HOST-174 |
| `season.hpp` | aforo dinámico por estación y clima | HOST-174 |
| `avatar.hpp` | jugador simulado (IA y entrada humana), percepción y carga | HOST-174 / HOST-175 |
| `world.hpp` (laboratorio) | escenarios largos con digesto y ajuste de parámetros | HOST-173 |
| `society.hpp` | `Society` (reputación), `Pack` | HOST-153 |
| `planner.hpp` | `PlannerDriver`/`PlanRunner` sobre `Goap`/`NumericGoap` (dominio por plantilla), `PlanParams` | HOST-155, HOST-313 |
| `domain.hpp` | dominio de ejemplo de objetos/construcción, `SimInventory` | HOST-155 |
| `body.hpp` | `ChainBody` (IK FABRIK) y postura expresiva (`BodyPose`) | HOST-156 |
| `world.hpp` + `world_core.hpp` | `SimWorld`: población, grafo de rooms, LOD, entorno, terreno, objetos, economía, sensores/memoria, ciclo de vida, reproducción, `Tend`, planificación | HOST-153 |

`SimWorld` se parte en **base + derivada** (D13 de `ENGINE_STRUCTURE_REVIEW.md`): `world_core.hpp`
define `SimWorldCore` (población, grafo de regiones, clima/terreno/sociedad, LOD y ticks, más el
estado del mundo) y `world.hpp` define `SimWorld : SimWorldCore<...>` (planificación, reproducción,
objetos/economía, percepción/memoria, lenguaje y mapa mental). Los consumidores incluyen
`<eng/sim/world.hpp>` (la derivada trae la base) y usan la API pública de `SimWorld` sin cambios.

Los módulos están **verificados por test host** y con gate de codegen; al no tener todavía
un consumidor en una demo, están **NO VERIFICADOS por demo** (ver
[docs/testing/README.md](../../testing/README.md)). El primer juego de `games/` con
criaturas los verificará en el 68000.

## 13. Crecimiento

Líneas abiertas, en orden de valor: **consumidor real en `games/`** que ejercite todo en
el 68000; **percepción imperfecta** (ruidos sin fuente, olores que engañan) para emergencia
y errores creíbles; **capacidad por región** (aforo por bioma en vez de un tope global) para
poblaciones más fieles; y **tácticas de manada** más ricas (emboscada, relevos, roles
dinámicos). El laboratorio de escenarios y sus ajustes se documentan en
`docs/debugging/investigaciones/sim-ecosystem-scenarios.md`. Cada pieza entra con test host y su sonda de
codegen si toca el bucle por frame.

> Navegación general: [DOC-MAP-PRINCIPAL.md](../../ai-dev-environment/DOC-MAP-PRINCIPAL.md).
> IA clásica reutilizada: [GAME_AI_LIBRARY.md](GAME_AI_LIBRARY.md).
