# Tests HOST — board

Categoría `board` de la batería host (L1). El índice de categorías está en [../README.md](../README.md) y la taxonomía en [docs/testing/TAXONOMY.md](../../../docs/testing/TAXONOMY.md).

## Catálogo

| ID | Test | Qué cubre |
|----|------|-----------|
| HOST-138 | [board_core](138_board_core/README.md) | `eng/board/core/{types,zobrist,game}.hpp`: tipos base (color, pieza, casilla 0x88, `Move`, `Score`), generador Zobrist y concepto `GameRules`. |
| HOST-139 | [board_budget](139_board_budget/README.md) | `eng/board/core/budget.hpp`: perfiles `P20`…`P1M`, selección por RAM libre y reparto de TT/libro/caché/pila. |
| HOST-140 | [chess_movegen](140_chess_movegen/README.md) | `eng/board/rules/chess/`: tablero 0x88, `make`/`unmake` con Zobrist incremental, generación legal validada por **perft** (inicial/Kiwipete/al paso/promoción) y fin de partida. |
| HOST-141 | [chess_draw_endgame](141_chess_draw_endgame/README.md) | `eng/board/rules/chess/{history,endgame}.hpp`: repetición de 3 posiciones (ventana de 50 movimientos) y finales teóricos (K vs K, K+B vs K, K+R/K+Q vs K, K+P vs K por la regla del cuadrado). |
| HOST-142 | [chess_notation](142_chess_notation/README.md) | `eng/board/rules/chess/notation.hpp`: SAN (peón, enroque, captura, promoción, desambiguación, jaque/mate) y UCI. |
| HOST-143 | [chess_search](143_chess_search/README.md) | `eng/board/search/search.hpp`: negamax + alpha-beta + iterative deepening + quiescence; mate en 1, dama colgada, recaptura, presupuesto de nodos y `StopToken`. |
| HOST-144 | [chess_tt_ordering](144_chess_tt_ordering/README.md) | `eng/board/search/tt.hpp` (entrada de 12 B, sondeo/escritura/reemplazo/reuso) y `rules/chess/ordering.hpp` (MVV-LVA y killer). |
| HOST-145 | [chess_eval](145_chess_eval/README.md) | `eng/board/eval/{chess_eval,features}.hpp`: evaluación (material+PST+movilidad+peones+desarrollo) y rasgos de desarrollo. |
| HOST-146 | [board_storage](146_board_storage/README.md) | `eng/board/storage/block_source.hpp` (`BlockSource` 3 estados y `RamBlockSource`) y `knowledge/cache.hpp` (caché LRU de bloques). |
| HOST-147 | [board_knowledge](147_board_knowledge/README.md) | `eng/board/knowledge/{book,endgame_tables}.hpp`: libro ordenado por clave, nombres, tablas de finales y round-trip byte a byte por bloque. |
| HOST-148 | [chess_b5](148_chess_b5/README.md) | `eng/board/search/{pruning,time,search}.hpp`: null-move, `TimeManager`, PV/Multi-PV y análisis paralelo determinista. |
| HOST-149 | [chess_explain](149_chess_explain/README.md) | `eng/board/explain/`: NLG por plantillas ES/EN (jaque, material, dama prematura, desarrollo, rey en el centro), tono y truncado seguro. |
| HOST-151 | [board_file](151_board_file/README.md) | `eng/board/storage/file_block_source.hpp`: E/S real de bloques desde un fichero del PC; cadena entradas→fichero→`BlockCache`→`probe_book`. |
| HOST-179 | [go_rules](179_go_rules/README.md) | `eng/board/rules/go/`: tablero 9×9, grupos/libertades, captura, suicidio y ko simple; jugada con flag de captura. |
| HOST-180 | [go_search](180_go_search/README.md) | `eng/board/eval/go_eval.hpp` + `rules/go/rules.hpp`: territorio/capturas/atari, `GoSearcher` (negamax/αβ/TT) encuentra la captura. |
| HOST-181 | [chess_variants](181_chess_variants/README.md) | `rules/chess/variant.hpp` (Chess960: 960 disposiciones y enroque generalizado) y `tournament.hpp` (torneos rápidos con presupuesto de nodos). |
| HOST-182 | [go_complete](182_go_complete/README.md) | Go: pase/dos pases (`GameEnded`), superko (historial de claves) y apertura (`knowledge/patterns.hpp`). |
| HOST-183 | [chess_variants_cond](183_chess_variants_cond/README.md) | Ajedrez: variantes de condición King of the Hill y Three-check vía `variant_score`. |
| HOST-184 | [chess_knowledge](184_chess_knowledge/README.md) | Ajedrez: sonda del libro de aperturas (`opening.hpp`) y finales teóricos en la evaluación. |
| HOST-187 | [pgn](187_pgn/README.md) | `rules/chess/pgn.hpp` (escritor PGN sin heap ni I/O) y `rules/chess/opening_book.hpp` (líneas de apertura incorporadas que comparten demo y selfplay). |
