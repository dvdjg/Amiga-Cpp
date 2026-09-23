# HOST-154: mente ampliada, conocimiento, jerarquía, genética y colonia (`eng::sim`)

Test host de la segunda capa de `eng::sim`: vida social, aprendizaje, jerarquía y
comportamiento de enjambre. Valida `engine/include/eng/sim/`:

`knowledge.hpp`, `mind.hpp`, `hierarchy.hpp`, `genetics.hpp` y `colony.hpp`.

## Qué comprueba

1. **Conocimiento** (`knowledge.hpp`): aprender y refinar una creencia, saturación de la
   confianza, consulta (`knows`/`confidence_for`/`best_knowledge`), olvido a largo plazo
   (`decay_knowledge`), desalojo de la creencia más débil al llenarse y **transmisión**
   (`share`) con la pérdida paramétrica `share_loss`.
2. **Mente ampliada** (`mind.hpp`): doce emociones (`emotion_count == 12`); los recuerdos
   negativos (traición, sometimiento) alimentan ira, tristeza y odio y enfrían el amor;
   los positivos (ayuda, enseñanza) alimentan amor, compasión y cordialidad; `attitude_toward`
   resume la actitud hacia un actor concreto.
3. **Jerarquía y libertad** (`hierarchy.hpp`): `contest_power`, `rank_band`, y las reglas
   de sumisión/rebeldía: el débil se somete; el fuerte no; alta autonomía y poca
   deferencia encarecen someterse y suben la presión de resistir.
4. **Genética** (`genetics.hpp`): herencia con sesgo de dominancia (el hijo toma genes de
   A o B según `dominance_bias`), mutación acotada, expresión del genoma en la
   personalidad (`genome_to_personality`) y clasificación de **castas** por umbrales
   (reina, obrera, soldado, zángano, exploradora, nodriza).
5. **Colonia y estigmergia** (`colony.hpp`): censo de castas, reparto del rol más
   necesario (`needed_caste`) según proporciones de diseño, y feromonas depositadas,
   seguidas y decaídas sobre `eng::ai::InfluenceMap` (reutilizado, no duplicado).
6. **Afecto dirigido** (`relationship.hpp`): `affinity` (vínculo) + `affect` (carga
   emocional) por individuo; `bond_score`, `most_loved`/`most_hated` y `adjust_affect`
   permiten celos y venganza selectiva.
7. **Ciclo de vida** (`lifecycle.hpp`): etapas por edad, madurez, `can_reproduce`,
   crecimiento saturado, muerte natural, herencia de la cría (`newborn_genome`) y sesgo
   de casta de una puesta (`bias_for_caste`).

## Salida de referencia

```
Sim mind/society:
OK: Sim mind/society (conocimiento, emociones, jerarquia, genetica, colonia, afecto dirigido, ciclo de vida)
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/sim/154_sim_mind_society
```
