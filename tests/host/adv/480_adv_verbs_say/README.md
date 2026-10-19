# HOST-480 — verbos, frase, puntero y réplicas (`eng::adv`)

Test host del hito **A1** de [`ROADMAP_ADVENTURE.md`](../../../../docs/guides/roadmap/ROADMAP_ADVENTURE.md) en su parte pura (sin mundo ni hardware): la tabla de verbos como contenido, la validación de la frase, la prioridad del hit-test de puntero y la cola de réplicas.

- `eng::adv::VerbSet<MaxVerbs>` (`engine/include/eng/adv/verb.hpp`): alta de verbos hasta capacidad (`kInvalidVerb` al llenar), consultas (`valid`/`at`/`size`), búsqueda por semántica (`find`) y cursor por verbo (`cursor_for`, `Default` si el id no existe).
- `resolve_action`: tabla verbo×objetos — aridad `Target` (objetivo sí, sujeto no), `TwoObjects` («Usar»/«Dar»: sujeto y objetivo), verbo inválido, y contrato de **no tocar `out`** al fallar.
- `pick_pointer`: prioridad **GUI → hotspot (el más al frente) → suelo** con el mismo hit-test por profundidad de `hotspot.hpp` (HOST-475).
- `eng::adv::SayQueue<MaxLines>` (`engine/include/eng/adv/say.hpp`): FIFO de réplicas (`text_id`, hablante, VBlanks), `tick()` una vez por VBlank con duración **exacta** (la siguiente réplica no pierde el tick de la retirada), rechazo con cola llena y con duración cero, y `clear`.

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/adv/480_adv_verbs_say
```
