// ============================================================================
// arena: torneo rápido de ajedrez (variantes incluidas) sobre el engine.
// ============================================================================
//
// Enfrenta al motor consigo mismo (mismas condiciones para blancas y negras) en N
// partidas rápidas con presupuesto de nodos por jugada y arranques de variante
// (`standard` o `chess960` con semilla creciente). Sirve para medir cambios de
// evaluación/búsqueda sin hardware.
//
// Uso:
//   arena [games] [depth] [variant] [seed] [max_plies] [node_budget]
//   por defecto: 10 2 chess960 0 60 20000

#include <cstdio>
#include <cstdlib>

#include <eng/board/tournament.hpp>

using namespace eng::board;

int main(int argc, char** argv) {
	const eng::u32 games = (argc > 1) ? static_cast<eng::u32>(std::atoi(argv[1])) : 10u;
	const eng::u32 depth = (argc > 2) ? static_cast<eng::u32>(std::atoi(argv[2])) : 2u;
	const char* variant_arg = (argc > 3) ? argv[3] : "chess960";
	const eng::u16 seed = (argc > 4) ? static_cast<eng::u16>(std::atoi(argv[4])) : 0u;
	const eng::u32 max_plies = (argc > 5) ? static_cast<eng::u32>(std::atoi(argv[5])) : 60u;
	const eng::u64 node_budget = (argc > 6) ? static_cast<eng::u64>(std::atoi(argv[6])) : 20000u;

	const chess::ChessVariant variant =
	    (variant_arg[0] == '9' || variant_arg[0] == 'c') ? chess::ChessVariant::Chess960
	                                                     : chess::ChessVariant::Standard;

	const ArenaResult result =
	    arena_chess(games, depth, node_budget, variant, seed, max_plies);

	std::printf("arena: %s, %u partidas, profundidad %u, %llu nodos/jugada\n",
	            chess::variant_name(variant), result.games, depth,
	            static_cast<unsigned long long>(node_budget));
	std::printf("  blancas %u | negras %u | tablas %u | sin acabar %u\n", result.white_wins,
	            result.black_wins, result.draws, result.unfinished);
	return 0;
}
