# GOAP ampliado para simulación (`eng::ai` + `eng::sim`)

Este documento especifica la **versión ampliada y versátil** del GOAP del engine: qué se conserva de
la base existente, qué se añade y con qué contrato. No sustituye a las cabeceras actuales
(`eng/ai/planning/goap.hpp`, `eng/ai/planning/numeric_goap.hpp`) ni al planner de criaturas
(`eng/sim/planner.hpp`), sino que **los extiende**. El estado vigente de `eng::ai` está en
[`GAME_AI_LIBRARY.md`](GAME_AI_LIBRARY.md); el plan de adopción, en
[`ROADMAP_GAME_AI.md`](../../guides/roadmap/ROADMAP_GAME_AI.md).

## 1. Alcance y encaje

La base actual es deliberadamente sencilla y correcta:

- **GOAP booleano** (`goap.hpp`): hechos (`Fact`), precondiciones a 1/0, efectos de poner a 1/0 y
  coste; A* hacia adelante con heurística de objetivos pendientes y `plan_cached`.
- **GOAP numérico** (`numeric_goap.hpp`): además de los hechos, hasta **4 variables de nivel**
  (`u8`, 0..255) con `var_ge`/`var_le` como precondiciones y `add`/`sub`/`set_var` como efectos,
  saturados a 0..255; `plan_cached` y `plan_reusing`.
- **Capa de criatura** (`sim/planner.hpp`, `sim/domain.hpp`): `SimGoap`, `SimNumericGoap<N>`
  (dominio por plantilla), `PlanRunner`, `PlannerDriver`, `plans()` (curiosidad/autonomía) y
  un dominio de ejemplo.

Lo que se pide aquí es una versión **más expresiva y más barata de reutilizar**, sin perder el
footprint. Las mejoras se agrupan en seis ejes y cada uno se apoya en lo que ya existe:

```text
                         lo que YA hay                     lo que se AÑADE
  estado       hechos (32/64) o [hechos + 4 niveles]  ->  híbrido (hechos + MaxVars niveles)
  accion       flags y levels (ge/le, add/sub/set)     ->  + target (entidad) + coste dinamico
  busqueda     A* con MaxNodes (sin plan parcial)      ->  + anytime (mejor plan parcial)
  cache        plan_cached / plan_reusing              ->  + mascaras de dependencia + invalidacion selectiva
  coordinacion reglas de manada (pack.hpp)             ->  + plan compartido (lider -> miembros)
  jerarquia    (no existe; solo lo menciona el roadmap) ->  + macro-acciones (HTN ligero)
```

**Regla de diseño**: cada eje se implementa **extendiendo** la cabecera existente o añadiendo una
capa fina encima; nunca duplicando el planificador ni el dominio. El dominio sigue siendo del juego y
se instancia con un alias (`using Ai = eng::ai::Goap<>;`).

**Cabecera única vs. implementaciones separadas.** El booleano y el numérico **no son dos algoritmos,
son el mismo (familia A\* GOAP) con un eje más**: `NumericState` es estructuralmente `State` más
`u8 vars[MaxVars]`, y `applicable`/`apply`/`satisfies`/`goal_distance` son los del booleano más la
parte numérica. Por eso el diseño **no** añade una tercera cabecera ni mantiene dos forks: **generaliza
`planning/goap.hpp`** como el planificador único de la familia, con `MaxVars`, la política de **clave**
y la de **heurística** como parámetros de plantilla (el patrón ya usado en el repo: `Cross` del
navmesh, `Broadphase` del crowd). `numeric_goap.hpp` queda como **alias de compatibilidad**.

```cpp
// planning/goap.hpp — ÚNICO planificador de la familia A*-GOAP
template <eng::u16 MaxFacts = 32,     // 32 (clave u32) o 64 (dos palabras)
          eng::u8  MaxVars  = 0,      // 0 = booleano puro; 1..8 = niveles
          class    Key      = ExactKey,   // ExactKey | WideKey | HashedKey (política)
          class    Heur     = GoalDist>   // GoalDist | RelaxedHMax       (política)
class Goap { /* estado, acción, goal, planner, caché, anytime... */ };

using GoapBool = Goap<32, 0>;                  // el booleano de hoy
template <eng::u8 V> using NumericGoap = Goap<32, V>;  // HOST-185/186 sin cambios
```

La variación es de **política** (clave, heurística), no de algoritmo, y `if constexpr (MaxVars == 0)`
elimina del binario la ruta numérica: la instanciación booleana genera el mismo código que hoy (lo
comprueba `asm-audit`/`codegen-report`).

**Cuándo SÍ se parte en cabeceras distintas**: cuando cambia el **algoritmo**, no el número de
características. Son cabeceras propias el **HTN** (descomposición de tareas por métodos, no búsqueda
A\*) y un **plan lineal/scripted** (secuencia fija, sin búsqueda). Dentro de A\*-GOAP, todo por
parámetros; una variante minimalista solo si se **mide** que la instanciación genérica arrastra de más,
y aun así como **especialización de política**, no como fork.

## 2. Estado del mundo híbrido

El estado de la versión ampliada unifica **hechos** y **niveles** en una sola estructura, que es la
**generalización** de las dos actuales (`State` del booleano y `NumericState` del numérico). No es un
tipo nuevo de un tercer header: vive en el `Goap<...>` único, con la capacidad como parámetro:

```cpp
// Dentro de Goap<MaxFacts, MaxVars> (la clave sale de una politica interna; `plan` y
// `plan_relaxed` eligen la heuristica):
template <eng::u16 MaxFacts, eng::u8 MaxVars>
struct State {
    eng::util::BitSet<MaxFacts> facts {};
    eng::util::Array<eng::u8, MaxVars> levels {}; // vacío/elidido si MaxVars == 0
    // test/set/clear (hechos) y get/set/add_sat/sub_sat (niveles), saturación 0..255.
};
```

Con `MaxVars == 0` la estructura y el código generado son los de hoy (el array de niveles se elide);
con `MaxVars >= 1` aparecen `get`/`add_sat` y las precondiciones/efectos numéricos de
`numeric_goap.hpp`. El `Goap` booleano pasa a ser `Goap<32, 0>` y el numérico `Goap<32, V>`.

Los niveles son **enteros de nivel**: un valor decimal se representa escalado (el mismo criterio que
`numeric_goap.hpp`, p. ej. `Fixed` q4.4 → nivel `v*16`), de modo que toda la aritmética queda en
enteros de 8 bits y no aparecen *libcalls* en `-nostdlib` (`AGENTS.md` §1.10).

```text
   HybridState<32,8>   (1 palabra de hechos + 8 bytes de niveles = 12 B)

   facts  : [f0][f1][f2]...[f31]          1 bit por hecho booleano
   levels : [ H ][ E ][ F ][ ... ][ x8 ]  1 byte por nivel (0..255)
              ^    ^    ^
              |    |    `-- Fear
              |    `------- Energy
              `------------ Hunger
```

**Clave del estado**: para `MaxFacts <= 32` y `MaxVars <= 4` la clave sigue siendo **exacta de 64
bits** (u32 de hechos + 4 niveles), sin colisiones y sin dependencia de `unsigned long long` en el
68000 (mismo criterio que `numeric_goap.hpp`). Para `MaxVars > 4` la clave pasa a una **clave ancha**
hechos + `MaxVars` bytes (hasta 8 niveles caben en 12 bytes) comparada por igualdad, o a un **hash de
64 bits** con verificación por igualdad del estado; es la política la que decide, no el algoritmo.

La mezcla de tipos sigue el patrón del repo: el algoritmo del planner es genérico sobre la clave y el
coste, y el **escalar** del nivel lo elige el consumidor (`u8` de nivel directo, o el entero de un
`Fixed` escalado). No se fija un `Fixed` concreto en la cabecera.

## 3. Condiciones y efectos ricos

La versión ampliada generaliza las precondiciones y los efectos a **cuatro clases** que conviven en
la misma acción:

| Clase | Precondición | Efecto |
|---|---|---|
| Hecho | `require(mask)`, `forbid(mask)` | `produce(mask)`, `consume(mask)` |
| Nivel | `var_ge(v, min)`, `var_le(v, max)` | `add(v, delta)`, `sub(v, delta)`, `set_var(v, level)` |
| Objetivo | `target_required(bool)` | — (el `target` lo aporta el binding) |
| Coste | — | `cost` estático o `cost_fn` (§4) |

Las dos primeras clases ya existen (`goap.hpp` y `numeric_goap.hpp`); la ampliación **no cambia su
semántica**, solo permite mezclarlas en el mismo planner híbrido y subir el número de niveles. La
aritmética de niveles es **saturada** (0..255) y determinista, sin `float` ni libcalls.

```text
   Eat    : pre  var_ge(Hunger, 8)          eff  sub(Hunger, 6), add(Energy, 2), consume(HasFood)
   Rest   : pre  forbid(Threatened)         eff  add(Energy, 4), add(Hunger, 1)
   Flee   : pre  var_ge(Fear, 6) & var_le(DistanceToTarget, 4)
                                            eff  sub(Fear, 4), set_var(DistanceToTarget, 0)
```

## 4. Acciones con parámetros y coste dinámico

Dos extensiones hacen que el mismo dominio sirva para "coger **este** objeto", "atacar a **ese**
enemigo" o "ir a **esa** sala", sin multiplicar acciones:

- **`target` (entidad)**: la acción declara un **hueco** de objetivo (`EntityId` / `RoomId`), que el
  juego **liga** al planificar (el mismo `SimActionKind` sirve para cualquier instancia). El planner
  trata el `target` como parte de la clave de la acción cuando cambia el resultado, de modo que dos
  ejecuciones con objetivos distintos no comparten entrada de caché.
- **`cost_fn`**: el coste puede depender del estado (distancia, peligro, terreno) mediante una
  `FunctionRef<eng::u16(const HybridState&)>` o una tabla; por defecto es el `cost` estático. Es lo
  que permite que "huir por el camino peligroso" cueste más que "huir por el refugio".

```cpp
template <eng::u16 MaxFacts, eng::u8 MaxVars>
struct HybridAction {
    eng::u16 id = 0;
    const char* name = nullptr;                 // solo depuracion
    eng::util::BitSet<MaxFacts> require {}, forbid {}, produce {}, consume {};
    VarCond  conditions[4] {};  eng::u8 num_conditions = 0;
    VarEffect effects[4] {};    eng::u8 num_effects    = 0;
    eng::u16 base_cost = 1;
    eng::u16 (*cost_fn)(const HybridState<MaxFacts, MaxVars>&) = nullptr; // opcional
};
```

## 5. Planificación *anytime* con presupuesto

En un 68000 el planner no puede bloquear el frame: la búsqueda lleva un **presupuesto de
expansiones** y devuelve el **mejor plan parcial** encontrado si se agota. El plan parcial **no** se
cachea (solo los completos), pero permite que la criatura **empiece a actuar** y siga refinando en
frames posteriores (*pondering*).

El planner booleano (`Goap`, `goap.hpp`) ya expone `set_budget(n)` (0 = sin límite) y
`partial()`. El parcial es el mejor nodo visitado (menor `h`; a igual `h`, mayor avance
`g`) y **solo se devuelve si el presupuesto corta la búsqueda**; si el espacio se agota sin
objetivo, no hay solución (0 acciones). Sin presupuesto el comportamiento es el histórico
(HOST-107); verificado por HOST-320.

```text
   plan_lazy(start, goal, budget) -> Plan{ actions[], length, cost, complete }

   completo      : alcanzo el goal  -> se cachea
   parcial       : gasto el presupuesto -> NO se cachea, se ejecuta y se replanifica
   emergencia    : ni siquiera hay parcial -> el llamador cae al comportamiento reactivo
                   (utilidad / `behavior.hpp`), que siempre existe
```

Presupuestos orientativos (`max_expansions`), ajustables por carga de CPU:

| Máquina | `max_expansions` | Efecto típico |
|---|---|---|
| A500 (512 KB) | 20 – 35 | planes parciales frecuentes |
| A500 + 512 KB | 40 – 60 | casi siempre plan completo |
| A1200 / 1 MB+ | 80 – 120 | planes óptimos en la mayoría de casos |

## 6. Caché de planes e invalidación selectiva

La caché ya existe (`plan_cached`, `plan_reusing`). La ampliación añade **dependencias** por entrada
y **invalidación selectiva**, para no tirar toda la caché cuando solo cambia el hambre de una
criatura:

Los dos planners implementan ya la invalidación selectiva: cada entrada guarda sus
dependencias (`used_facts` = unión de `pre_true`/`pre_false`/`eff_add`/`eff_del` de las
acciones del plan, y `used_vars` en el numérico) e `invalidate_selective(changed)` descarta
solo las entradas afectadas, compactando el pool; `clear_plan_cache()` sigue siendo el
vaciado total (HOST-321 y HOST-316). El planner numérico añade además la política
**LRU+menos-usos** (desaloja la entrada con menos `hits` al llenarse, HOST-316).

```cpp
struct PlanCacheEntry {
    eng::u32 key = 0;               // clave (hash) del estado + objetivo
    eng::u64 used_facts = 0;        // hechos que tocaron las acciones del plan
    eng::u8  used_vars  = 0;        // bit i = nivel i usado (hasta 8)
    // ... plan (ids), age, hits, valid
};

/// Solo se invalidan las entradas que dependían de lo que cambió.
void invalidate_selective(eng::u64 changed_facts, eng::u8 changed_vars) noexcept;
```

Las máscaras se acumulan **al insertar** (`used_facts |= require|forbid|produce|consume`,
`used_vars |= (1 << nivel)` por cada condición/efecto) y el mundo, al cambiar un hecho o un nivel,
pasa la máscara de lo cambiado. Política de reemplazo: **LRU + menos usos** (la entrada más vieja y
menos reutilizada es la víctima). Cuándo usar cada invalidación:

| Cambio | Invalidación total | Selectiva | Mejor opción |
|---|---|---|---|
| Sube el hambre de una criatura | borra todo | solo planes de alimentación | selectiva |
| Aparece una amenaza (hecho) | borra todo | solo planes de huida/ataque/defensa | selectiva |
| Cambia la distancia a un objetivo | borra todo | solo planes que usan ese nivel | selectiva |
| Cambian las reglas del mundo (nueva zona, clima global) | correcto | — | total |

## 7. Planificación coordinada (manada / colonia)

Hoy las manadas usan **reglas** (`pack.hpp`, `coordinate_packs`). La ampliación permite que el
**líder** planifique y los miembros reciban objetivos, sin duplicar el planner:

```text
   lider:  plan_lazy(start_lider, goal_manada) -> Plan compartido
                     |
                     +-- reparte objetivos por miembro segun PackRole / Relationship / Signal
                           (los miembros NO planifican: ejecutan el paso que les toca)

   uso de Relationship / Signal / PackRole como PRECONDICIONES y EFECTOS del dominio de manada
```

El plan es **uno** (del líder); los miembros lo consumen como objetivos locales. Así el coste de
planificación no crece con el tamaño del grupo.

## 8. Jerárquico (HTN ligero)

Para planes largos, una **macro-acción** se expande a un sub-plan: el nodo hoja del A* puede ser una
acción simple o una compuesta que encadena un sub-dominio. El planner no cambia (sigue siendo A*);
solo se añade la noción de "acción compuesta" y su tabla de sub-pasos. Es lo último del roadmap:
solo se adopta cuando aparezcan misiones de varios objetivos encadenados.

## 9. Integración en `eng::sim`

El punto de unión sigue siendo `sim/planner.hpp`. El **dominio GOAP es un parámetro de
plantilla** (booleano ligero por defecto; numerico/hibrido cuando el objetivo tiene
magnitudes) y la **política de planificación** añade presupuesto e histeresis:

```text
   PlannerDriver<MaxNodes, MaxSteps, DomainT>
     +-- sim/domain y needs   -> fija start (hechos) y goal (hechos + niveles)
     +-- DomainT (plantilla)  -> SimGoap (booleano ligero) | SimNumericGoap<N> (magnitudes)
     +-- PlannerParams        -> cuando planificar (curiosidad/autonomia + intervalo + histeresis)
     +-- budget (max_expansions) segun maquina y carga
     +-- PlanRunner           -> avanza un paso por tick (ya existe)

   Decision por tick: utilidad (`behavior.hpp`) elige QUE hacer;
   GOAP solo entra en los comportamientos que requieren varios pasos.
```

El dominio por defecto es el booleano (`SimGoap` = `Goap<32,0>`): elegir el numerico no
cambia el algoritmo ni el footprint del caso booleano (HOST-322).

El planner de `HybridState` ocupa miles de bytes: se instancia en memoria estatica o de fondo
(`world_core.hpp::PlannerHolder`), **nunca** en la pila del 68000.

## 10. Presupuesto de memoria y CPU (A500)

| Componente | Memoria aproximada |
|---|---|
| `HybridState<32,8>` + 20 acciones híbridas | 3.5 – 4.5 KB |
| Caché de planes (24 entradas + máscaras) | 0.6 – 0.8 KB |
| Nodos A* (48) | 2.0 – 2.5 KB |
| **Total GOAP ampliado** | **≈ 6 – 8 KB** |

Asumible en un A500 de 512 KB, igual que la base actual. La mejora clave es que el **coste por
decisión** baja (caché + anytime + presupuesto), no que el footprint suba.

## 11. Variantes y cuándo usar cada una

| Variante | Cuándo | Evitar cuando |
|---|---|---|
| `Goap<32, 0>` (booleano) | tareas de "estado de cosas" (tiene/hizo/fue) | hay magnitudes (hambre, distancia) |
| `Goap<32, 1..4>` (numérico) | pocas magnitudes, clave exacta de 64 bits, coste mínimo | hacen falta más de 4 magnitudes |
| `Goap<32, 5..8>` (híbrido) | magnitudes y hechos mezclados, objetivos con entidad | no hay planificación multi-paso |
| Anytime + presupuesto | poco CPU, reacciones rápidas, pondering | el plan debe ser óptimo sí o sí |
| Invalidación selectiva | mundos dinámicos con muchas magnitudes | mundo casi estático |
| Coordinado (líder) | manadas, colonias, formaciones | agentes independientes |
| Jerárquico (HTN) | misiones de varios objetivos encadenados | planes cortos (<8 pasos) |

## 12. Verificación

Cada mejora se valida en host antes de integrarla en el mundo (`AGENTS.md` §1.5), reutilizando los
tests existentes y añadiendo los suyos:

- **Existentes**: `HOST-107` (GOAP booleano), `HOST-185`/`HOST-186` (GOAP numérico y relajado),
  `HOST-155` (planificador de criatura), `HOST-153` (integración en el mundo).
- **Equivalencia (red de seguridad de la unificación)**: el `Goap<32, 0>` generalizado debe producir
  el **mismo plan** que el `Goap` booleano actual (y `Goap<32, V>` el mismo que `NumericGoap<V>`) en
  HOST-107/185/186, sin cambiar la lógica de esos tests.
- **Añadir**: estado híbrido + clave ancha, condiciones/efectos mixtos, *anytime* (parcial vs.
  completo), invalidación selectiva (que no invalide de más), coste dinámico, acción con `target`, y
  plan compartido de manada.
- **Criterio**: equivalencia con la referencia clásica (A* exhaustivo) en los casos pequeños; el plan
  *anytime* no debe ser **peor** que el presupuesto permite, y la invalidación selectiva no debe
  dejar pasar un plan inválido (caso negativo).

## Referencias

- `docs/engine/architecture/GAME_AI_LIBRARY.md` — estado vigente de `eng::ai` (§3 GOAP, §3.5 numérico).
- `docs/engine/architecture/SIM_ECOSYSTEM.md` — modelo de criatura y mundo que consume el planner.
- `docs/engine/architecture/NPC_PSYCHOLOGY.md` — personalidad/necesidades que alimentan el estado.
- `docs/guides/roadmap/ROADMAP_GAME_AI.md` — adopción por fases (G7).
- `engine/include/eng/ai/planning/goap.hpp`, `numeric_goap.hpp`, `engine/include/eng/sim/planner.hpp`.
