// ============================================================================
// Juego 100: AJEDREZ (motor eng::board sobre el engine C++23)
// ============================================================================
//
// Primer consumidor real de `eng::board`. Pone en hardware las piezas del motor:
//
//   * REGLAS: `eng::board::ChessRules` (0x88, generacion legal, jaque, tablas,
//     repeticion con `PositionHistory`).
//   * BUSQUEDA: `ChessSearcher` (negamax + alpha-beta + iterative deepening +
//     quiescence + TT) elige la jugada del rival (negras) a profundidad limitada.
//   * EVALUACION/NLG: `eng::board::chess::explain` describe la posicion en espanol
//     con los rasgos de la evaluacion.
//
// Modo de juego: el jugador lleva las BLANCAS con el joystick (puerto 0); el motor
// juega las NEGRAS. Cursor con la cruz; FIRE selecciona pieza y FIRE de nuevo en el
// destino mueve. La barra inferior muestra el turno, la ultima jugada del motor y la
// explicacion.
//
// Display: `StaticEhbScene` (320x256, 6 planos EHB). El tablero y el texto se
// rasterizan a los bitplanes por CPU (patron de la demo 060) porque el tablero solo
// cambia cuando hay jugada o el cursor se mueve; no hace falta Blitter.
//
// Build/run/analyze (mismos wrappers que una demo):
//   bash tools/build/build-demo.sh games/100_chess --debug --clean
//   bash tools/run/run-demo.sh games/100_chess
//
// Verificacion: build -> run -> analyze (pendiente de ejecucion en emulador).

#include <eng/board/explain/explain.hpp>
#include <eng/board/rules/chess/history.hpp>
#include <eng/board/rules/chess/notation.hpp>
#include <eng/board/rules/chess/rules.hpp>
#include <eng/board/rules/chess/variant.hpp>
#include <eng/core/types.hpp>
#include <eng/debug/run_status.hpp>
#include <eng/engine.hpp>
#include <eng/graphics/drivers/ehb_scene.hpp>
#include <eng/graphics/font8.hpp>
#include <eng/input/input.hpp>
#include <eng/platform/amiga_minimal.hpp>
#include <eng/platform/input_poll.hpp>

#include <exec/execbase.h>
#include <proto/exec.h>

#include "support/gcc8_c_support.h"

struct ExecBase* SysBase = nullptr;

extern "C" {
__attribute__((used)) volatile eng::debug::RunStatus g_eng_run_status {
	eng::debug::run_status_magic,
	eng::debug::run_status_version,
	static_cast<eng::u16>(eng::debug::RunState::Cold),
	0,
	0,
};
}

namespace {

namespace drivers = eng::graphics::drivers;
using namespace eng::board;
using namespace eng::board::chess;

using Position = ChessRules::Position;
using MoveList = ChessRules::MoveList;

constexpr eng::u16 kScreenW = drivers::StaticEhbScene::width;
constexpr eng::u16 kScreenH = drivers::StaticEhbScene::height;
constexpr eng::u16 kBytesPerRow = drivers::StaticEhbScene::bytes_per_row;
constexpr eng::u8 kPlanes = drivers::StaticEhbScene::plane_count;
constexpr eng::u32 kPlaneBytes = drivers::StaticEhbScene::plane_bytes;

// Geometria del tablero: casillas de 24x24, origen (8,16) -> 192x192.
constexpr eng::s32 kSquare = 24;
constexpr eng::s32 kBoardX = 8;
constexpr eng::s32 kBoardY = 16;

// Indices de color EHB (paleta base 0..31).
constexpr eng::u8 kColorLight = 2;  // casilla clara
constexpr eng::u8 kColorDark = 3;   // casilla oscura
constexpr eng::u8 kColorCursor = 4; // cruz del cursor
constexpr eng::u8 kColorWhite = 31; // piezas/tiempo blancas
constexpr eng::u8 kColorBlack = 8;  // piezas negras
constexpr eng::u8 kColorText = 20;  // texto de estado
constexpr eng::u8 kColorWarn = 26;  // jaque/aviso

// El buscador vive en memoria estatica (TT + tablas de PV; no en la pila del 68000).
ChessSearcher g_searcher {};

// Variante y semilla de arranque. Cambia a `Standard` para ajedrez clásico o deja
// `Chess960` con otra semilla (0..959) para "piezas descolocadas" reproducibles.
constexpr ChessVariant kVariant = ChessVariant::Chess960;
constexpr eng::u16 kSeed = 0u;

struct ChessGame {
	void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		m_memory_ok = backend.configure_memory({96u * 1024u, 16u * 1024u, 4u * 1024u});

		const drivers::EhbPalette palette {
			0x000, 0x123, 0x5b3, 0x263, 0xff0, 0xf00, 0x000, 0x000,
			0x210, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
			0x000, 0x000, 0x000, 0x000, 0x0cf, 0x000, 0x000, 0x000,
			0x000, 0x000, 0xf40, 0x000, 0x000, 0x000, 0xff0, 0xfff,
		};
		const drivers::StaticEhbSceneConfig scene_config {&palette, nullptr, 0, 1024};
		m_scene_ok = m_scene.init(backend.memory(), scene_config);
		if (!m_memory_ok || !m_scene_ok) {
			eng::debug::mark_failed(g_eng_run_status, 0x00010001u);
			return;
		}

		m_pos = initial_position(kVariant, kSeed);
		m_history.push(m_pos.key);
		m_cursor = make_square(4u, 1u); // e2
		m_selected = kNoSquare;
		m_status[0] = '\0';
		copy_text(m_status, "Tu turno (blancas): mueve con el joystick");

		redraw();
		backend.takeover_display(m_scene.copper_words_ptr());
		eng::debug::mark_ready(g_eng_run_status, 0x000100FFu);
	}

	void update(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		(void)backend;
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);

		eng::input::InputAggregator input;
		eng::amiga::poll_input(input);
		bool changed = false;
		if (input.pad0.left && square_file(m_cursor) > 0u) {
			m_cursor = static_cast<Square>(m_cursor - 1u);
			changed = true;
		}
		if (input.pad0.right && square_file(m_cursor) < 7u) {
			m_cursor = static_cast<Square>(m_cursor + 1u);
			changed = true;
		}
		if (input.pad0.up && square_rank(m_cursor) < 7u) {
			m_cursor = static_cast<Square>(m_cursor + 16u);
			changed = true;
		}
		if (input.pad0.down && square_rank(m_cursor) > 0u) {
			m_cursor = static_cast<Square>(m_cursor - 16u);
			changed = true;
		}

		if (input.pad0.fire && !m_fire_held) {
			on_fire();
			changed = true;
		}
		m_fire_held = input.pad0.fire;

		if (changed) {
			redraw();
		}
	}

	void render(eng::amiga::MinimalBackend&, eng::GameContext& context) {
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	// --- Logica de juego ---

	void on_fire() {
		if (terminal_is_over(terminal(m_pos))) {
			return;
		}
		if (m_selected == kNoSquare) {
			const Piece piece = m_pos.board[m_cursor];
			if (piece != kEmptyPiece && piece_color(piece) == Color::White) {
				m_selected = m_cursor;
			}
			return;
		}
		const Move move = find_move(m_selected, m_cursor);
		if (move_none(move)) {
			m_selected = kNoSquare; // cancelar seleccion
			return;
		}
		apply_move(move);
		m_selected = kNoSquare;
		if (terminal_is_over(terminal(m_pos))) {
			report_terminal();
			return;
		}
		ai_move();
		report_terminal();
	}

	Move find_move(Square from, Square to) const {
		MoveList legal;
		generate_legal(m_pos, legal);
		for (eng::usize i = 0u; i < legal.size(); ++i) {
			const Move move = legal[i];
			if (move_from(move) != from || move_to(move) != to) {
				continue;
			}
			// Promocion del jugador: dama por defecto.
			if (move_promo(move) != PieceType::None && move_promo(move) != PieceType::Queen) {
				continue;
			}
			return move;
		}
		return kNoMove;
	}

	void apply_move(Move move) {
		Undo undo;
		make_move(m_pos, move, undo);
		m_history.push(m_pos.key);
	}

	void ai_move() {
		if (in_check(m_pos, Color::Black) && !has_legal(m_pos)) {
			return;
		}
		ChessSearcher::Limits limits {2u, 200000u};
		const ChessSearcher::Result result = g_searcher.search(m_pos, limits);
		if (move_none(result.best_move)) {
			return;
		}
		char san[12];
		const eng::usize n = to_san(m_pos, result.best_move, eng::Span<char> {san, sizeof(san)}, false);
		for (eng::usize i = 0u; i < n && i < sizeof(m_ai_san) - 1u; ++i) {
			m_ai_san[i] = san[i];
		}
		m_ai_san[n < sizeof(m_ai_san) ? n : (sizeof(m_ai_san) - 1u)] = '\0';
		Undo undo;
		make_move(m_pos, result.best_move, undo);
		m_history.push(m_pos.key);
	}

	bool has_legal(const Position& pos) {
		MoveList legal;
		generate_legal(pos, legal);
		return legal.size() != 0u;
	}

	void report_terminal() {
		const Terminal t = terminal(m_pos);
		if (t == Terminal::Checkmate) {
			const bool black_mated = in_check(m_pos, Color::Black);
			copy_text(m_status, black_mated ? "JAQUE MATE: ganan las blancas"
			                               : "JAQUE MATE: ganan las negras");
		} else if (t == Terminal::Stalemate) {
			copy_text(m_status, "Rey ahogado: tablas");
		} else if (t == Terminal::Draw50 || t == Terminal::Repetition ||
		           t == Terminal::InsufficientMaterial) {
			copy_text(m_status, "Tablas");
		} else {
			copy_text(m_status, "Tu turno.");
			if (m_ai_san[0] != '\0') {
				append_text(m_status, eng::util::StringView {" Motor: "});
				append_text(m_status, eng::util::StringView {m_ai_san});
				append_text(m_status, eng::util::StringView {"."});
			}
			append_text(m_status, eng::util::StringView {" "});
			char text[200];
			const eng::usize n = explain(m_pos,
			                             ExplainOptions {Language::Spanish, Tone::Neutral, 2u},
			                             eng::Span<char> {text, sizeof(text)});
			append_text(m_status, eng::util::StringView {text, n});
		}
	}

	// --- Dibujo (directo a bitplanes, patron de la demo 060) ---

	void put_pixel_exact(eng::u8* planes, eng::s32 x, eng::s32 y, eng::u8 color) {
		if (x < 0 || y < 0 || x >= static_cast<eng::s32>(kScreenW) ||
		    y >= static_cast<eng::s32>(kScreenH)) {
			return;
		}
		const eng::u32 byte = static_cast<eng::u32>(y) * kBytesPerRow + (static_cast<eng::u32>(x) >> 3u);
		const eng::u8 bit = static_cast<eng::u8>(0x80u >> (static_cast<eng::u32>(x) & 7u));
		for (eng::u8 pl = 0; pl < kPlanes; ++pl) {
			eng::u8* p = planes + static_cast<eng::u32>(pl) * kPlaneBytes + byte;
			if ((color & (1u << pl)) != 0u) {
				*p = static_cast<eng::u8>(*p | bit);
			} else {
				*p = static_cast<eng::u8>(*p & static_cast<eng::u8>(~bit));
			}
		}
	}

	void fill_rect(eng::u8* planes, eng::s32 x, eng::s32 y, eng::s32 w, eng::s32 h, eng::u8 color) {
		for (eng::s32 py = 0; py < h; ++py) {
			for (eng::s32 px = 0; px < w; ++px) {
				put_pixel_exact(planes, x + px, y + py, color);
			}
		}
	}

	void draw_glyph(eng::u8* planes, eng::s32 x, eng::s32 y, char c, eng::u8 color) {
		for (eng::u8 row = 0; row < eng::Font8::kRows; ++row) {
			const eng::u8 glyph = eng::Font8::row(static_cast<eng::u16>(static_cast<eng::u8>(c)), row);
			for (eng::u8 k = 0; k < 8u; ++k) {
				if ((glyph & (1u << k)) != 0u) {
					put_pixel_exact(planes, x + k, y + row, color);
				}
			}
		}
	}

	void draw_text(eng::u8* planes, eng::s32 x, eng::s32 y, const char* text, eng::u8 color) {
		for (eng::s32 i = 0; text[i] != '\0'; ++i) {
			draw_glyph(planes, x, y, text[i], color);
			x += 8;
		}
	}

	void redraw() {
		eng::u8* planes = m_scene.bitplanes().data();
		// Limpia toda la pantalla (rapido: escritura por bytes).
		for (eng::u32 i = 0u; i < kPlaneBytes * kPlanes; ++i) {
			planes[i] = 0u;
		}

		// 1) Casillas y piezas.
		for (eng::s32 rank = 0; rank < 8; ++rank) {
			for (eng::s32 file = 0; file < 8; ++file) {
				const eng::s32 x = kBoardX + file * kSquare;
				const eng::s32 y = kBoardY + (7 - rank) * kSquare;
				const bool light = ((file + rank) & 1) == 0;
				fill_rect(planes, x, y, kSquare, kSquare, light ? kColorLight : kColorDark);

				const Square square = make_square(static_cast<eng::u8>(file), static_cast<eng::u8>(rank));
				const Piece piece = m_pos.board[square];
				if (piece != kEmptyPiece) {
					const char letter = piece_letter(piece_type(piece));
					const eng::u8 color = (piece_color(piece) == Color::White) ? kColorWhite : kColorBlack;
					draw_glyph(planes, x + 8, y + 8, letter, color);
				}
			}
		}

		// 2) Cursor (marco de 2 px) y seleccion.
		const eng::s32 cx = kBoardX + static_cast<eng::s32>(square_file(m_cursor)) * kSquare;
		const eng::s32 cy = kBoardY + (7 - static_cast<eng::s32>(square_rank(m_cursor))) * kSquare;
		const eng::u8 cursor_color =
		    (m_selected != kNoSquare && m_selected == m_cursor) ? kColorWarn : kColorCursor;
		fill_rect(planes, cx, cy, kSquare, 2, cursor_color);
		fill_rect(planes, cx, cy + kSquare - 2, kSquare, 2, cursor_color);
		fill_rect(planes, cx, cy, 2, kSquare, cursor_color);
		fill_rect(planes, cx + kSquare - 2, cy, 2, kSquare, cursor_color);

		// 3) Barra de estado.
		if (terminal_is_over(terminal(m_pos)) || in_check(m_pos, to_move(m_pos))) {
			draw_text(planes, 8, kBoardY + 8 * kSquare + 8, m_status, kColorWarn);
		} else {
			draw_text(planes, 8, kBoardY + 8 * kSquare + 8, m_status, kColorText);
		}
	}

	static char piece_letter(PieceType type) {
		switch (type) {
		case PieceType::Pawn: return 'P';
		case PieceType::Knight: return 'N';
		case PieceType::Bishop: return 'B';
		case PieceType::Rook: return 'R';
		case PieceType::Queen: return 'Q';
		case PieceType::King: return 'K';
		default: return '?';
		}
	}

	static void copy_text(char* dst, const char* src) {
		eng::usize i = 0u;
		while (src[i] != '\0' && i < kStatusCap - 1u) {
			dst[i] = src[i];
			++i;
		}
		dst[i] = '\0';
	}

	static void append_text(char* dst, eng::util::StringView text) {
		eng::usize i = 0u;
		while (dst[i] != '\0') {
			++i;
		}
		for (eng::usize k = 0u; k < text.size() && i < kStatusCap - 1u; ++k) {
			dst[i++] = text[k];
		}
		dst[i] = '\0';
	}

	static constexpr eng::usize kStatusCap = 120;

	drivers::StaticEhbScene m_scene {};
	bool m_memory_ok = false;
	bool m_scene_ok = false;
	Position m_pos {};
	PositionHistory<256> m_history {};
	Square m_cursor = make_square(4u, 1u);
	Square m_selected = kNoSquare;
	bool m_fire_held = false;
	char m_status[kStatusCap] {};
	char m_ai_san[12] {};
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::MinimalBackend backend {};
	ChessGame game {};
	eng::Engine engine {backend, game};
	engine.run_frames_polling(0xffff);

	return 0;
}
