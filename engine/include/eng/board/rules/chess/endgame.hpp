#pragma once

/// \file endgame.hpp
/// Sondeo de **finales teóricos** de ajedrez: reconoce por firma de material las
/// posiciones con resultado conocido (tablas o ganadas) y devuelve un veredicto
/// barato, para que la búsqueda no tenga que demostrarlo y para que el motor sepa
/// "buscar tablas" cuando va peor.
///
/// Qué reconoce:
/// - tablas de material insuficiente (K vs K, K+menor vs K, K+B vs K+B del mismo
///   color);
/// - finales ganados elementales (K+R vs K, K+Q vs K, K+B+N vs K, K+Q vs K+R);
/// - **K+P vs K** con la *regla del cuadrado*: el rey defensor alcanza la casilla
///   de promoción si su distancia de Chebyshev a ella no supera los avances del
///   peón, en cuyo caso es tablas.
///
/// La regla del cuadrado es exacta para peones centrales; los peones de banda (a/h)
/// tienen matices de ahogamiento que el motor resuelve con búsqueda. Se declara el
/// resultado como *desconocido* en esos casos en vez de arriesgar un veredicto.
///
/// Verificación: HOST-141.

#include <eng/board/core/types.hpp>
#include <eng/board/rules/chess/board.hpp>

namespace eng::board::chess {

/// Tipo de final reconocido.
enum class EndgameKind : u8 {
	Unknown = 0u,
	KvK,
	KNvK,
	KBvK,
	KBvKB,
	KRvK,
	KQvK,
	KBNvK,
	KQvKR,
	KPvK,
};

/// Veredicto del sondeo. `is_draw`/`is_win` solo son válidos si `known`.
struct EndgameProbe {
	EndgameKind kind = EndgameKind::Unknown;
	bool known = false;
	bool is_draw = false;
	bool is_win = false;
};

/// Recuento de material por bando (peones y figuras; reyes aparte).
struct MaterialCount {
	u8 pawns[2] {};
	u8 knights[2] {};
	u8 bishops[2] {};
	u8 rooks[2] {};
	u8 queens[2] {};
	u8 bishops_light[2] {}; ///< alfiles en casillas claras (para K+B vs K+B)

	[[nodiscard]] u8 total(int side) const noexcept {
		return static_cast<u8>(pawns[side] + knights[side] + bishops[side] + rooks[side] +
		                       queens[side]);
	}
};

/// Cuenta el material de una posición.
[[nodiscard]] inline MaterialCount count_material(const Position& pos) noexcept {
	MaterialCount m {};
	for (int raw = 0; raw < 128; ++raw) {
		if (!square_valid(static_cast<Square>(raw))) {
			continue;
		}
		const Piece piece = pos.board[raw];
		if (piece == kEmptyPiece) {
			continue;
		}
		const int side = static_cast<int>(piece_color(piece));
		switch (piece_type(piece)) {
		case PieceType::Pawn: ++m.pawns[side]; break;
		case PieceType::Knight: ++m.knights[side]; break;
		case PieceType::Bishop: {
			++m.bishops[side];
			const bool light = ((square_file(static_cast<Square>(raw)) +
			                     square_rank(static_cast<Square>(raw))) & 1u) != 0u;
			if (light) {
				++m.bishops_light[side];
			}
			break;
		}
		case PieceType::Rook: ++m.rooks[side]; break;
		case PieceType::Queen: ++m.queens[side]; break;
		case PieceType::King:
		case PieceType::None:
			break;
		}
	}
	return m;
}

/// Distancia de Chebyshev (máxima de fila/columna) entre dos casillas.
[[nodiscard]] inline int chebyshev(Square a, Square b) noexcept {
	const int df = static_cast<int>(square_file(a)) - static_cast<int>(square_file(b));
	const int dr = static_cast<int>(square_rank(a)) - static_cast<int>(square_rank(b));
	const int af = df < 0 ? -df : df;
	const int ar = dr < 0 ? -dr : dr;
	return af > ar ? af : ar;
}

/// K+P vs K: ¿es tablas por la regla del cuadrado? `known=false` para peón de banda
/// (ahogamiento/corner), que la búsqueda debe resolver.
[[nodiscard]] inline bool kpk_is_draw(const Position& pos, MaterialCount& m, bool& known) noexcept {
	known = true;
	// Localiza el peón y el rey defensor.
	const int pawn_side = (m.pawns[0] != 0u) ? 0 : 1;
	const Color attacker = static_cast<Color>(pawn_side);
	const Color defender = opposite(attacker);
	Square pawn = kNoSquare;
	for (int raw = 0; raw < 128; ++raw) {
		if (square_valid(static_cast<Square>(raw)) &&
		    pos.board[raw] == make_piece(attacker, PieceType::Pawn)) {
			pawn = static_cast<Square>(raw);
			break;
		}
	}
	if (pawn == kNoSquare) {
		known = false;
		return false;
	}
	const u8 file = square_file(pawn);
	if (file == 0u || file == 7u) {
		// Peón de banda: la regla del cuadrado no basta.
		known = false;
		return false;
	}
	const u8 promo_rank = (attacker == Color::White) ? 7u : 0u;
	const Square promo = make_square(file, promo_rank);
	const int advances = static_cast<int>((attacker == Color::White)
	                                          ? (promo_rank - square_rank(pawn))
	                                          : (square_rank(pawn) - promo_rank));
	const Square def_king = king_square(pos, defender);
	if (def_king == kNoSquare) {
		known = false;
		return false;
	}
	return chebyshev(def_king, promo) <= advances;
}

/// Sondea la posición y devuelve el final reconocido con su veredicto.
[[nodiscard]] inline EndgameProbe probe_endgame(const Position& pos) noexcept {
	EndgameProbe probe {};
	MaterialCount m = count_material(pos);
	const u8 w = m.total(0);
	const u8 b = m.total(1);

	// K+P vs K (un único peón y nada más).
	if (m.pawns[0] + m.pawns[1] == 1u && w + b == 1u) {
		probe.kind = EndgameKind::KPvK;
		bool known = false;
		const bool draw = kpk_is_draw(pos, m, known);
		probe.known = known;
		probe.is_draw = draw;
		probe.is_win = known && !draw;
		return probe;
	}
	if (m.pawns[0] + m.pawns[1] != 0u) {
		return probe; // final con peones no reconocido
	}

	// Sin peones: casos elementales.
	if (w == 0u && b == 0u) {
		probe.kind = EndgameKind::KvK;
		probe.known = true;
		probe.is_draw = true;
		return probe;
	}
	// K + una pieza MENOR vs K (caballo o alfil): tablas.
	if (w + b == 1u &&
	    (m.knights[0] + m.knights[1] + m.bishops[0] + m.bishops[1]) == 1u) {
		probe.kind = (m.knights[0] + m.knights[1] != 0u) ? EndgameKind::KNvK : EndgameKind::KBvK;
		probe.known = true;
		probe.is_draw = true;
		return probe;
	}
	if (m.knights[0] + m.knights[1] == 0u && m.bishops[0] == 1u && m.bishops[1] == 1u &&
	    m.rooks[0] + m.rooks[1] + m.queens[0] + m.queens[1] == 0u) {
		probe.kind = EndgameKind::KBvKB;
		probe.known = true;
		// Mismo color de casilla: tablas teóricas.
		probe.is_draw = (m.bishops_light[0] == m.bishops_light[1]);
		return probe;
	}
	if (m.rooks[0] + m.rooks[1] == 1u && w + b == 1u) {
		probe.kind = EndgameKind::KRvK;
		probe.known = true;
		probe.is_win = true;
		return probe;
	}
	if (m.queens[0] + m.queens[1] == 1u && w + b == 1u) {
		probe.kind = EndgameKind::KQvK;
		probe.known = true;
		probe.is_win = true;
		return probe;
	}
	if (m.knights[0] + m.knights[1] == 1u && m.bishops[0] + m.bishops[1] == 1u &&
	    m.rooks[0] + m.rooks[1] + m.queens[0] + m.queens[1] == 0u) {
		probe.kind = EndgameKind::KBNvK;
		probe.known = true;
		probe.is_win = true;
		return probe;
	}
	if (m.queens[0] + m.queens[1] == 1u && m.rooks[0] + m.rooks[1] == 1u && w + b == 2u) {
		probe.kind = EndgameKind::KQvKR;
		probe.known = true;
		probe.is_win = true;
		return probe;
	}
	return probe;
}

} // namespace eng::board::chess
