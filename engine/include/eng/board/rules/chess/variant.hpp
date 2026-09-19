#pragma once

/// \file variant.hpp
/// **Variantes de ajedrez**. La primera y más habitual con "piezas descolocadas" es
/// **Chess960 / Fischer Random**: la fila inicial se baraja con la restricción de
/// alfiles en colores opuestos, rey entre torres y las dos torres en extremos. El
/// enroque se generaliza en `movegen.hpp`/`board.hpp` (rey y torres en columnas
/// arbitrarias), así que el resto del motor (búsqueda, evaluación, GPL, NLG) no
/// cambia.
///
/// `set_start_960` usa la numeración estándar de Scharnagl (0..959), de modo que la
/// misma semilla produce siempre la misma posición (tournaments reproducibles).
///
/// Verificación: HOST-181.

#include <eng/board/rules/chess/board.hpp>

namespace eng::board::chess {

/// Variantes soportadas. `Standard` y `Chess960` comparten reglas; solo cambia la
/// posición inicial (y el enroque generalizado ya cubre 960). `KingOfTheHill` y
/// `ThreeCheck` añaden una **condición de victoria** (`variant_score`).
enum class ChessVariant : u8 {
	Standard = 0u,
	Chess960 = 1u,
	KingOfTheHill = 2u,
	ThreeCheck = 3u,
	Count,
};

/// Nombre corto de la variante (depuración/informes).
[[nodiscard]] constexpr const char* variant_name(ChessVariant variant) noexcept {
	switch (variant) {
	case ChessVariant::Chess960: return "chess960";
	case ChessVariant::KingOfTheHill: return "king-of-the-hill";
	case ChessVariant::ThreeCheck: return "three-check";
	case ChessVariant::Standard:
	default: return "standard";
	}
}

/// Distribuye la fila inicial de Chess960 segun la numeracion de Scharnagl.
/// `back_rank[file]` recibe el `PieceType` de cada columna.
[[nodiscard]] inline bool chess960_back_rank(u16 index, PieceType back_rank[8]) noexcept {
	if (index >= 960u) {
		return false;
	}
	u16 n = index;
	bool used[8] = {};

	// Alfiles en casillas de color opuesto (claro: 1,3,5,7; oscuro: 0,2,4,6).
	const u8 light = static_cast<u8>((n % 4u) * 2u + 1u);
	n = static_cast<u16>(n / 4u);
	const u8 dark = static_cast<u8>((n % 4u) * 2u);
	n = static_cast<u16>(n / 4u);
	back_rank[light] = PieceType::Bishop;
	back_rank[dark] = PieceType::Bishop;
	used[light] = true;
	used[dark] = true;

	// Dama en la q-esima casilla libre.
	const u8 q = static_cast<u8>(n % 6u);
	n = static_cast<u16>(n / 6u);
	u8 free_sq[8];
	u8 free_count = 0u;
	for (u8 file = 0u; file < 8u; ++file) {
		if (!used[file]) free_sq[free_count++] = file;
	}
	back_rank[free_sq[q]] = PieceType::Queen;
	used[free_sq[q]] = true;

	// Caballos: k-esima combinacion de 2 entre las 5 casillas libres.
	const u8 k = static_cast<u8>(n % 10u);
	u8 knight_sq[5];
	u8 knight_count = 0u;
	for (u8 file = 0u; file < 8u; ++file) {
		if (!used[file]) knight_sq[knight_count++] = file;
	}
	u8 combo = 0u;
	u8 ka = 0u;
	u8 kb = 0u;
	for (u8 a = 0u; a < knight_count; ++a) {
		for (u8 b = static_cast<u8>(a + 1u); b < knight_count; ++b) {
			if (combo == k) {
				ka = knight_sq[a];
				kb = knight_sq[b];
			}
			++combo;
		}
	}
	back_rank[ka] = PieceType::Knight;
	back_rank[kb] = PieceType::Knight;
	used[ka] = true;
	used[kb] = true;

	// Las tres casillas restantes reciben R K R (torres en los extremos).
	u8 rest[3];
	u8 rest_count = 0u;
	for (u8 file = 0u; file < 8u; ++file) {
		if (!used[file]) rest[rest_count++] = file;
	}
	back_rank[rest[0]] = PieceType::Rook;
	back_rank[rest[1]] = PieceType::King;
	back_rank[rest[2]] = PieceType::Rook;
	return true;
}

/// Posición inicial de Chess960 para `index` (0..959).
inline void set_start_960(Position& pos, u16 index) noexcept {
	PieceType back_rank[8] {};
	(void)chess960_back_rank(index, back_rank);
	pos = Position {};
	for (u8 file = 0u; file < 8u; ++file) {
		pos.board[make_square(file, 0u)] = make_piece(Color::White, back_rank[file]);
		pos.board[make_square(file, 1u)] = make_piece(Color::White, PieceType::Pawn);
		pos.board[make_square(file, 6u)] = make_piece(Color::Black, PieceType::Pawn);
		pos.board[make_square(file, 7u)] = make_piece(Color::Black, back_rank[file]);
	}
	pos.castling = static_cast<u8>(kCastleWhiteKing | kCastleWhiteQueen | kCastleBlackKing |
	                               kCastleBlackQueen);
	rebuild_castle_rooks(pos);
	pos.key = compute_key(pos);
}

/// Posición inicial de una variante. `seed` elige la disposición en Chess960.
[[nodiscard]] inline Position initial_position(ChessVariant variant, u16 seed) noexcept {
	Position pos;
	if (variant == ChessVariant::Chess960) {
		set_start_960(pos, static_cast<u16>(seed % 960u));
	} else {
		set_start(pos);
	}
	return pos;
}

/// Puntuación de la condición de victoria de una variante (0 si no aplica). Se usa
/// en la búsqueda: si el bando al turno ya perdió/ganó por la condición, devuelve un
/// score de mate.
template <ChessVariant V>
[[nodiscard]] inline Score variant_score_impl(const Position& pos) noexcept {
	if constexpr (V == ChessVariant::KingOfTheHill) {
		const auto on_center = [](Square sq) {
			return sq == make_square(3u, 3u) || sq == make_square(4u, 3u) ||
			       sq == make_square(3u, 4u) || sq == make_square(4u, 4u);
		};
		const Color stm = to_move(pos);
		if (on_center(king_square(pos, opposite(stm)))) {
			return static_cast<Score>(-kScoreMate);
		}
		if (on_center(king_square(pos, stm))) {
			return kScoreMate;
		}
		return 0;
	} else if constexpr (V == ChessVariant::ThreeCheck) {
		const Color stm = to_move(pos);
		if (pos.checks[static_cast<u8>(opposite(stm))] >= 3u) {
			return static_cast<Score>(-kScoreMate);
		}
		if (pos.checks[static_cast<u8>(stm)] >= 3u) {
			return kScoreMate;
		}
		return 0;
	} else {
		return 0;
	}
}

} // namespace eng::board::chess
