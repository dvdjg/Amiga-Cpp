# HOST-152: modelo de criatura (`eng::sim`)

Test host del modelo de simulación de ecosistema: los componentes de una criatura y su
decisión por utilidad. Valida `engine/include/eng/sim/`:

`types.hpp`, `needs.hpp`, `personality.hpp`, `species.hpp`, `relationship.hpp`,
`tracker.hpp`, `mind.hpp`, `behavior.hpp` y `creature.hpp`.

## Qué comprueba

1. **Presupuesto por plantilla**: `Needs` (7 B), `Personality` (10 B) y `Relationship` (6 B)
   tienen tamaño fijo; `AbstractCreature<MaxTrackers,MaxRelations>` crece solo si la
   plantilla pide más capacidad. El coste de RAM se decide en compilación.
2. **Necesidades** (`Needs`): tick lineal (hambre/cansancio/social suben, miedo se
   apacigua), saturación, `any_critical` y las acciones `feed`/`rest`/`calm`/`socialize`/
   `heal`/`hurt`. La **exposición ambiental** es genérica: `set_exposure(kind, severidad)`
   cubre lluvia, frío, calor, tormenta, polvo, radiación e inundación, y cada peligro
   sugiere su protección (`shelter_for`).
3. **Personalidad** (`trait_mod`) y **modificador** (`apply_mod`): conversión entera de un
   rasgo `[0,100]` a porcentaje `[-100,+100]` y su efecto sobre un score, con recortes.
4. **Relaciones** (`Relationship`, `StaticVector`): alta/actualización, ajuste de afinidad
   con recorte, desalojo de la relación más débil y búsqueda de la más intensa.
5. **Trackers**: observación con refresco de posición, olvido por `decay`, desalojo por
   confianza y rechazo cuando lo percibido no mejora lo ya sabido.
6. **Mente** (`Mind`): doce emociones, memoria episódica acotada (envejece y desaloja),
   balance de valencia de los recuerdos y actualización afectiva por pasos.
7. **Decisión** (`score_behaviors`/`choose_behavior`): huir/cazar/comer/dormir/socializar/
   buscar refugio, con modificadores de personalidad y afecto; `SimTraitsLean` elimina en
   compilación los módulos sociales; la elección respeta la histéresis.
8. **Criatura** (`AbstractCreature`): daño/curación, flags de herida y refugio, y el
   vínculo entre estar realizado y estar vivo.

## Salida de referencia

```
Sim creature:
tamanos: Needs=7 Personality=10 Emotions=12 Mind=48 Tracker=14 Rel=6 Creature<6,6>=344 Creature<2,2>=264 Creature<10,10>=424
OK: Sim creature (needs, personalidad, relaciones, trackers, mente, utilidad)
```

Los tamaños son los del host (64 bits: `usize` = 8). En el cruce `m68k` (`usize` = 4) el
gate de `tools/analyze/codegen-report.mjs` fija los valores de las piezas clave (`Needs` 7,
`Personality` 10, `Emotions` 12, `Mind` 44, `Tracker` 14, `Senses` 13, `Relationship` 6,
`Inventory` 12, `Item` 12, `AbstractCreature<>` 308 y `SimWorld<>` 23 998 B).

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/sim/152_sim_creature
```
