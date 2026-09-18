#pragma once

/// \file types.hpp
/// Tipos base de `eng::board`: color, pieza, casilla **0x88**, `Move`, `Score` y
/// `MoveList`. Son game-agnostic (los usan ajedrez y Go) y freestanding.
///
/// Decisiones de diseño (68000):
/// - Tablero **0x88**: la casilla es `rank<<4 | file` y la validez de una casilla
///   se comprueba con `(s & 0x88) == 0` (una máscara, sin comparaciones). Evita los
///   bitboards de 64 bits que el 68000 no tiene de forma nativa.
/// - `Move` empaquetado en 32 bits: `from` (7 bits, casilla 0x88), `to` (7 bits) y
///   un `payload` de 16 bits que cada juego interpreta (captura, enroque,
///   promoción…). La TT puede guardarlo sin desempaquetar.
/// - `Score` es `s16` en centipeones (ajedrez) o puntos (Go); nunca coma flotante.
///
/// Verificación: HOST-138.

#include <eng/core/scalar.hpp>
#include <eng/core/types.hpp>
#include <eng/core/util/static_vector.hpp>

namespace eng::board {

/// Entero de trabajo del motor de tablero, **elegido en compilación** por máquina
/// (`eng::intw`: `s16` en 68000, `s32` en 68020, `int` en host). Se usa en
/// acumuladores e índices cuyo rango cabe en palabra (evaluación, rasgos, offsets,
/// conteos). Las puntuaciones que exceden 16 bits (ordenación por encima de 32 k,
/// nodos) usan tipos explícitos `s32`/`u64`, donde el coste es imprescindible.
using board_int = eng::intw;

/// Bando. Se guarda como `u8` (no `bool`) porque viaja en structs de estado.
enum class Color : u8 {
	White = 0u,
	Black = 1u,
};

[[nodiscard]] constexpr Color opposite(Color color) noexcept {
	return color == Color::White ? Color::Black : Color::White;
}

/// Tipo de pieza. `None` es el valor de una casilla vacía.
enum class PieceType : u8 {
	None = 0u,
	Pawn = 1u,
	Knight = 2u,
	Bishop = 3u,
	Rook = 4u,
	Queen = 5u,
	King = 6u,
};

/// Pieza empaquetada: bit 3 = color, bits 0..2 = tipo. `0` = casilla vacía.
using Piece = u8;
inline constexpr Piece kEmptyPiece = 0u;

[[nodiscard]] constexpr Piece make_piece(Color color, PieceType type) noexcept {
	return static_cast<Piece>((static_cast<u8>(color) << 3u) | static_cast<u8>(type));
}

[[nodiscard]] constexpr PieceType piece_type(Piece piece) noexcept {
	return static_cast<PieceType>(piece & 0x07u);
}

[[nodiscard]] constexpr Color piece_color(Piece piece) noexcept {
	return static_cast<Color>((piece >> 3u) & 1u);
}

[[nodiscard]] constexpr bool piece_empty(Piece piece) noexcept { return piece == kEmptyPiece; }

/// Casilla en geometría **0x88**: `rank << 4 | file`, con `rank`/`file` en 0..7.
using Square = u8;
inline constexpr u8 kFiles = 8u;
inline constexpr u8 kRanks = 8u;
inline constexpr u8 kBoardSize = 128u;
inline constexpr Square kNoSquare = 0x80u;

[[nodiscard]] constexpr bool square_valid(Square square) noexcept {
	return (square & 0x88u) == 0u;
}

[[nodiscard]] constexpr Square make_square(u8 file, u8 rank) noexcept {
	return static_cast<Square>(static_cast<u8>((rank << 4u) | file));
}

[[nodiscard]] constexpr u8 square_file(Square square) noexcept {
	return static_cast<u8>(square & 0x07u);
}

[[nodiscard]] constexpr u8 square_rank(Square square) noexcept {
	return static_cast<u8>((square >> 4u) & 0x07u);
}

/// Índice compacto 0..63 (rank*8+file), para tablas Zobrist y PST.
[[nodiscard]] constexpr u8 compact_square(Square square) noexcept {
	return static_cast<u8>(static_cast<u8>(square_rank(square) * 8u + square_file(square)));
}

/// Puntuación entera: centipeones en ajedrez, puntos en Go.
using Score = s16;
inline constexpr Score kScoreNone = static_cast<Score>(-32000);
inline constexpr Score kScoreMate = static_cast<Score>(30000);
inline constexpr Score kScoreInfinite = static_cast<Score>(32000);
inline constexpr Score kMateThreshold = static_cast<Score>(kScoreMate - 1000);

[[nodiscard]] constexpr bool score_is_mate(Score score) noexcept {
	return score >= kMateThreshold || score <= static_cast<Score>(-kMateThreshold);
}

/// Jugada empaquetada. `from`/`to` son casillas 0x88; `payload` lo define el juego.
using Move = u32;
inline constexpr Move kNoMove = 0u;
inline constexpr u32 kMoveFromShift = 0u;
inline constexpr u32 kMoveToShift = 7u;
inline constexpr u32 kMovePayloadShift = 14u;

[[nodiscard]] constexpr Move make_move(Square from, Square to, u16 payload = 0u) noexcept {
	return static_cast<Move>(from) | (static_cast<Move>(to) << kMoveToShift) |
	       (static_cast<Move>(payload) << kMovePayloadShift);
}

[[nodiscard]] constexpr Square move_from(Move move) noexcept {
	return static_cast<Square>(move & 0x7fu);
}

[[nodiscard]] constexpr Square move_to(Move move) noexcept {
	return static_cast<Square>((move >> kMoveToShift) & 0x7fu);
}

[[nodiscard]] constexpr u16 move_payload(Move move) noexcept {
	return static_cast<u16>((move >> kMovePayloadShift) & 0xffffu);
}

[[nodiscard]] constexpr bool move_none(Move move) noexcept { return move == kNoMove; }

/// Lista de jugadas de capacidad fija (máximo legal en ajedrez ≈ 218; 256 da margen).
using MoveList = eng::util::StaticVector<Move, 256u>;

} // namespace eng::board
