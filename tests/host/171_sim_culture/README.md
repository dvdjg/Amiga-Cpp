# HOST-171: cultura y rituales (`eng::sim`)

Test host de `engine/include/eng/sim/culture.hpp`: tradiciones que se heredan por enseñanza
y se representan en grupo.

## Qué comprueba

1. **Conocimiento ritual** (`learn_ritual`/`knows_ritual`/`ritual_confidence`): el ritual es
   un conocimiento (`KnowledgeKind::Ritual`, sujeto = `RitualKind`).
2. **`signal_for_ritual`**: cómo se expresa cada ritual (saludo, luto, llamada de caza...).
3. **`perform_ritual`**: efecto emocional (el luto consuela, el festejo alegra, la caza
   enfurece).
4. **`ritual_for_event`**: qué ritual se dispara ante un evento según lo aprendido.
5. **Herencia y actuación**: `share` transmite el ritual y `SimWorld::enact_ritual` lo
   actúa en la región (efecto propio + señal percibida).

## Salida de referencia

```
Sim culture:
OK: Sim culture (rituales, efecto, disparo, ensenanza, mundo)
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/171_sim_culture
```
