# Tests HOST — cards

Categoría `cards` de la batería host (L1). El índice de categorías está en [../README.md](../README.md) y la taxonomía en [docs/testing/TAXONOMY.md](../../../docs/testing/TAXONOMY.md).

## Catálogo

| ID | Test | Qué cubre |
|----|------|-----------|
| HOST-188 | [cards_core](188_cards_core/README.md) | `eng/cards/core/{types,deck,budget}.hpp`: tipos de carta, baraja determinista (mismo PRNG+semilla) y presupuesto de naipes `N20`…`N512`. |
| HOST-189 | [cards_hand_rank](189_cards_hand_rank/README.md) | `eng/cards/rules/hand_rank.hpp`: evaluador de 5/7 cartas por conteo, categorías, orden total, escalera de as bajo y kickers. |
| HOST-190 | [cards_holdem](190_cards_holdem/README.md) | `eng/cards/rules/texas_holdem.hpp`: reparto/ciegas, calles, acciones legales, resolución por retirada y botes laterales. |
| HOST-191 | [cards_equity](191_cards_equity/README.md) | `eng/cards/eval/equity.hpp`: equity Monte Carlo determinista, heurística preflop y pot odds. |
| HOST-192 | [cards_selfplay](192_cards_selfplay/README.md) | `eng/cards/{ai/bot,sim/session}.hpp`: estilos, modelo de rival, sesiones CPU vs CPU, conservación y determinismo. |
| HOST-193 | [cards_range](193_cards_range/README.md) | `eng/cards/eval/range.hpp`: 169 clases de mano inicial, `HandRange`, equity contra rango y tabla preflop (`build_preflop_table`). |
| HOST-194 | [cards_variants](194_cards_variants/README.md) | `eng/cards/rules/variants.hpp` + `evaluate_omaha`: Omaha (2 hole + 3 board), reparto por variante y estructura Limit (apuesta fija y tope de subidas). |
| HOST-195 | [cards_wildcards](195_cards_wildcards/README.md) | Comodines: mazo de 54 cartas, sustitución por la mejor carta en `evaluate_hand`, heurística preflop y showdown con comodín. |
| HOST-196 | [cards_omaha](196_cards_omaha/README.md) | Omaha de extremo a extremo: `equity_vs_random_omaha` (4 cartas) y `run_session` con `variant=Omaha` (conservación, determinismo y comodines). |
| HOST-197 | [cards_stud](197_cards_stud/README.md) | `eng/cards/rules/seven_stud.hpp`: Seven-Card Stud (ante, bring-in, 5 calles, showdown y retirada). |
| HOST-198 | [cards_five_draw](198_cards_five_draw/README.md) | `eng/cards/rules/five_draw.hpp` + `evaluate_deuces_wild`: Five-Card Draw con descarte y Deuces Wild. |
| HOST-204 | [cards_persona](204_cards_persona/README.md) | `eng/cards/ai/persona_bot.hpp`: parámetros del bot por arquetipo/estado, emisión de tells, lectura de rivales y rango combinado con tells. |
