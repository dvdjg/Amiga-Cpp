// Lanzar:
//   Depurar   : bash ./tools/build/build-demo.sh demos/features/board/chess/amiga/123_chess_match --debug   && bash ./tools/run/run-demo.sh demos/features/board/chess/amiga/123_chess_match --keep-running
//   Optimizada: bash ./tools/build/build-demo.sh demos/features/board/chess/amiga/123_chess_match --release && bash ./tools/run/run-demo.sh demos/features/board/chess/amiga/123_chess_match --keep-running

// ============================================================================
// Demo 123: "chess match" — dos IAs de ajedrez se enfrentan con estilos distintos
// ============================================================================
//
// Escenario: Amiga 500 con ampliacion (512 KB Chip + 512 KB Slow). Dos motores del
// engine `eng::board` juegan una partida completa con reloj; cada uno piensa con su
// PROPIA instancia de partida, su tabla de transposicion y su estilo de evaluacion:
//
//   * BLANCAS  "agresivo":  material y movilidad (busca iniciativa/material).
//   * NEGRAS   "posicional": peones y desarrollo (busca estructura/coordinacion).
//
// Un JUEZ canta las jugadas en algebraica y comenta patrones (apertura del libro,
// dama prematura, retraso de desarrollo, rey en el centro, jaque/mate...).
//
// La demo trae las librerias de apertura **cargadas en memoria** (no hay acceso a
// sistema de archivos todavia): se construyen al arrancar con las reglas reales. Los
// finales teoricos los aporta `probe_endgame` dentro de la evaluacion.
//
// Layout (320x256, EHB): tablero 8x8 a la izquierda, panel de pensamiento de cada
// bando a la derecha y panel del juez abajo-izquierda.
//
// Build/run/analyze:
//   bash tools/build/build-demo.sh demos/features/board/chess/amiga/123_chess_match --debug --clean
//   bash tools/run/run-demo.sh demos/features/board/chess/amiga/123_chess_match

#include <eng/board/eval/styled_eval.hpp>
#include <eng/board/explain/explain.hpp>
#include <eng/board/knowledge/book.hpp>
#include <eng/board/rules/chess/history.hpp>
#include <eng/board/rules/chess/notation.hpp>
#include <eng/board/rules/chess/opening_book.hpp>
#include <eng/board/rules/chess/rules.hpp>
#include <eng/api/api.hpp>
#include <eng/core/util/static_string.hpp>
#include <eng/core/util/text.hpp>
#include <eng/graphics/font8.hpp>
#include <eng/platform/amiga/backend.hpp>

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

namespace scene = eng::graphics::composition;
using namespace eng::board;
using namespace eng::board::chess;
using eng::s32;
using eng::u8;
using eng::u16;
using eng::u32;
using eng::u64;
using eng::usize;

// --- Presupuesto de memoria por motor -------------------------------------
// 16384 entradas de TT (12 B) ~= 192 KB por motor; los dos caben en la Slow RAM de
// 512 KB junto al display. Subir a 32768 (~384 KB/motor) en maquinas con mas RAM da
// el objetivo de ~400 KB por jugador.
constexpr eng::u32 kDemoTtEntries = 16384u;
constexpr eng::u16 kMaxDepth = 12u;
// Una sola busqueda por jugada con presupuesto de nodos. Llamar a `search` por
// rebanadas (una por frame) reinicia la ID y paga la generacion de la raiz cada vez;
// medido en el A500, eso domina el coste. Ver docs/debugging/investigaciones/board-selfplay-and-perf.md.
constexpr eng::u64 kMoveNodes = 24u;

// --- Reloj -----------------------------------------------------------------
constexpr eng::s32 kStartMs = 300000; // 5:00
constexpr eng::s32 kIncrementMs = 0;  // sin incremento (partida tradicional)
constexpr eng::s32 kMoveCostMs = 200; // coste nominal por jugada de busqueda
constexpr eng::u32 kBookDelayFrames = 8u; // pausa teatral de una jugada de libro

// --- Eval por estilo (policy con pesos activos) ----------------------------
// `StyledEval` y `g_active_weights` viven en `eng/board/eval/styled_eval.hpp` para
// compartirlos con la simulacion host (`tools/board/selfplay.cpp`).

using DemoEngine = Searcher<ChessRules, StyledEval, ChessOrdering, kDemoTtEntries, false>;

// --- Libro de aperturas en memoria -----------------------------------------
constexpr eng::usize kBookMax = 64u;
BookEntry g_book[kBookMax] {};
eng::u32 g_book_count = 0u;

void build_book() {
	g_book_count = build_opening_book(eng::Span<BookEntry> {g_book, kBookMax});
}

eng::util::StringView book_name_of(const Position& pos) {
	const BookProbe probe =
	    probe_opening_book(pos, eng::Span<const BookEntry> {g_book, g_book_count});
	return probe.found ? opening_name(probe.name_id) : eng::util::StringView {};
}

// --- Jugador ---------------------------------------------------------------
struct Player {
	Position pos {};
	PositionHistory<512> history {};
	DemoEngine engine {};
	EvalWeights style = positional_weights();
	s32 clock_ms = kStartMs;
	u32 depth = 0u;
	eng::u64 nodes = 0u;
	Score score = 0;
	Move best = kNoMove;
	char last_san[12] {};
	bool use_book = false;
};

// --- Geometria / colores ---------------------------------------------------
constexpr eng::u16 kBytesPerRow = 40u;
constexpr eng::u8 kPlanes = 6u;
constexpr eng::u32 kPlaneBytes = 10240u;

constexpr eng::s32 kCell = 16;
constexpr eng::s32 kBoardX = 0;
constexpr eng::s32 kBoardY = 0;
constexpr eng::s32 kPanelX = 136;
constexpr eng::s32 kPanelChars = 22;

constexpr eng::u8 kColorLight = 2;
constexpr eng::u8 kColorDark = 3;
constexpr eng::u8 kColorWhitePiece = 31;
constexpr eng::u8 kColorBlackPiece = 24;
constexpr eng::u8 kColorText = 20;
constexpr eng::u8 kColorWhitePanel = 15;
constexpr eng::u8 kColorBlackPanel = 12;
constexpr eng::u8 kColorWarn = 30; // amarillo (PENSANDO / anuncios del juez)

// --- Dibujo ----------------------------------------------------------------
/// Rellena una region con `byte_w` bytes por fila a partir del byte `byte_x`
/// (escritura por bytes: rapido). Escribe el color exacto (0xff/0x00 por plano).
void fill_bytes(eng::u8* planes, eng::s32 byte_x, eng::s32 y, eng::s32 byte_w, eng::s32 h,
                eng::u8 color) {
	for (eng::s32 row = 0; row < h; ++row) {
		const eng::u32 base = static_cast<eng::u32>(y + row) * kBytesPerRow +
		                      static_cast<eng::u32>(byte_x);
		for (eng::s32 b = 0; b < byte_w; ++b) {
			for (eng::u8 pl = 0; pl < kPlanes; ++pl) {
				const eng::u8 v = (color & (1u << pl)) != 0u ? 0xffu : 0x00u;
				planes[static_cast<eng::u32>(pl) * kPlaneBytes + base + static_cast<eng::u32>(b)] = v;
			}
		}
	}
}

/// Invierte los 8 bits (Font8 usa bit0=izquierda; la pantalla usa 0x80=izquierda).
[[nodiscard]] eng::u8 bit_reverse8(eng::u8 v) {
	eng::u8 r = 0u;
	for (eng::u8 k = 0u; k < 8u; ++k) {
		if ((v & (1u << k)) != 0u) {
			r = static_cast<eng::u8>(r | (0x80u >> k));
		}
	}
	return r;
}

/// Dibuja un carácter en una posición alineada a byte (x % 8 == 0), escribiendo un
/// byte por plano y fila (rapido).
void draw_char_bytes(eng::u8* planes, eng::s32 x, eng::s32 y, char c, eng::u8 color) {
	const eng::u32 byte_x = static_cast<eng::u32>(x) >> 3u;
	for (eng::u8 row = 0; row < eng::Font8::kRows; ++row) {
		const eng::u8 glyph =
		    eng::Font8::row(static_cast<eng::u16>(static_cast<eng::u8>(c)), row);
		const eng::u8 rev = bit_reverse8(glyph);
		const eng::u32 base = static_cast<eng::u32>(y + row) * kBytesPerRow + byte_x;
		for (eng::u8 pl = 0; pl < kPlanes; ++pl) {
			const eng::u8 v = (color & (1u << pl)) != 0u ? rev : 0x00u;
			planes[static_cast<eng::u32>(pl) * kPlaneBytes + base] = v;
		}
	}
}

void draw_text_bytes(eng::u8* planes, eng::s32 x, eng::s32 y, const char* text, eng::u8 color,
                     eng::s32 max_chars) {
	eng::s32 cx = x;
	eng::s32 count = 0;
	for (eng::s32 i = 0; text[i] != '\0' && count < max_chars; ++i) {
		draw_char_bytes(planes, cx, y, text[i], color);
		cx += 8;
		++count;
	}
}

void draw_wrapped_bytes(eng::u8* planes, eng::s32 x, eng::s32 y, const char* text, eng::u8 color,
                        eng::s32 max_chars, eng::s32 max_lines) {
	eng::s32 line = 0;
	eng::s32 col = 0;
	eng::s32 cx = x;
	eng::s32 cy = y;
	for (eng::s32 i = 0; text[i] != '\0' && line < max_lines; ++i) {
		if (col >= max_chars) {
			col = 0;
			++line;
			cx = x;
			cy += 8;
			if (line >= max_lines) {
				break;
			}
		}
		draw_char_bytes(planes, cx, cy, text[i], color);
		cx += 8;
		++col;
	}
}

// --- Texto sin divisiones en caliente --------------------------------------
void append_text(char* dst, const char* src, eng::usize cap) {
	eng::usize i = 0u;
	while (dst[i] != '\0') {
		++i;
	}
	eng::usize k = 0u;
	while (src[k] != '\0' && i + 1u < cap) {
		dst[i++] = src[k++];
	}
	dst[i] = '\0';
}

void append_view(char* dst, eng::util::StringView text, eng::usize cap) {
	eng::usize i = 0u;
	while (dst[i] != '\0') {
		++i;
	}
	for (eng::usize k = 0u; k < text.size() && i + 1u < cap; ++k) {
		dst[i++] = text[k];
	}
	dst[i] = '\0';
}

/// Rellena con espacios hasta `kPanelChars` para que reescribir la linea borre
/// el contenido anterior de esa misma fila (refresco en vivo sin borrar el panel).
void pad_line(char* dst, eng::usize cap) {
	eng::usize i = 0u;
	while (dst[i] != '\0') {
		++i;
	}
	while (i + 1u < cap && i < static_cast<eng::usize>(kPanelChars)) {
		dst[i++] = ' ';
	}
	dst[i] = '\0';
}

void append_uint(char* dst, eng::u32 value, eng::usize cap) {
	eng::util::StaticString<12> digits;
	(void)eng::util::to_chars_u32(digits, value);
	append_text(dst, digits.c_str(), cap);
}

void append_score(char* dst, Score value, eng::usize cap) {
	const bool negative = value < 0;
	eng::u32 magnitude = static_cast<eng::u32>(negative ? -value : value);
	eng::u32 pawns = 0u;
	while (magnitude >= 100u) {
		magnitude -= 100u;
		++pawns;
	}
	eng::util::StaticString<12> digits;
	digits.append(negative ? '-' : '+');
	(void)eng::util::to_chars_u32(digits, pawns);
	digits.append('.');
	if (magnitude < 10u) {
		digits.append('0');
	}
	(void)eng::util::to_chars_u32(digits, magnitude);
	(void)cap;
	append_text(dst, digits.c_str(), cap);
}

// --- Juez ------------------------------------------------------------------
struct Judge {
	Position pos {};
	PositionHistory<512> history {};
	char announce[24] {};
	char comment[80] {};
	char result[48] {};
	u16 move_number = 1u;
};

// --- Partida ---------------------------------------------------------------
struct ChessMatch {
	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		m_memory_ok = backend.configure_memory({96u * 1024u, 16u * 1024u, 4u * 1024u});

		const eng::Palette32 palette {
			0x000, 0x123, 0x5b3, 0x263, 0xff0, 0xf00, 0x000, 0x000,
			0x000, 0x000, 0x000, 0x000, 0x224, 0x000, 0x000, 0x776,
			0x000, 0x000, 0x000, 0x000, 0x0cf, 0x000, 0x000, 0x000,
			0x610, 0x000, 0xf40, 0x000, 0x000, 0x000, 0xff0, 0xfff,
		};
		scene::SceneResources res = scene::planar(320, 256, 6);
		res.mode = scene::SceneMode::Ehb;
		m_scene_ok = scene::compose(m_scene, backend.memory(), res,
				    scene::ocs_a500,
					scene::display(res),
					scene::palette(eng::PaletteWords {palette.color, 32u}, 0u, 32u));
		if (!m_memory_ok || !m_scene_ok) {
			eng::debug::mark_failed(g_eng_run_status, 0x00012301u);
			return;
		}

		build_book();
		set_start(m_white.pos);
		m_white.history.push(m_white.pos.key);
		m_white.style = aggressive_weights();
		m_white.use_book = true;
		set_start(m_black.pos);
		m_black.history.push(m_black.pos.key);
		m_black.style = positional_weights();
		m_black.use_book = true;
		set_start(m_judge.pos);
		m_judge.history.push(m_judge.pos.key);

		m_turn = Color::White;
		m_phase = Phase::Thinking;
		start_turn();
		redraw();
		m_board_dirty = false;
		m_scene.takeover(backend);
		m_ready_sent = true;
		eng::debug::mark_ready(g_eng_run_status, 0x000123FFu);
	}

	void update(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		(void)backend;
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
		const eng::u32 frame = context.frame.frame_index;

		if (m_phase != Phase::GameOver) {
			Player& p = current();
			if (m_phase == Phase::BookDelay) {
				if (m_book_delay > 0u) {
					--m_book_delay;
				} else {
					commit(p.best, true);
				}
			} else {
				// Una busqueda por jugada con presupuesto de nodos (no se reinicia la
				// ID frame a frame): mucho menos coste fijo por jugada en el A500.
				g_active_weights = p.style;
				const DemoEngine::Result result =
				    p.engine.search(p.pos, {kMaxDepth, kMoveNodes});
				p.nodes = static_cast<eng::u64>(result.nodes);
				if (!move_none(result.best_move)) {
					p.depth = result.depth;
					p.score = result.score;
					p.best = result.best_move;
					commit(p.best, false);
				} else {
					end_game("Sin jugada legal");
				}
			}
		}

		// Redibuja solo cuando cambia el tablero; refresca los paneles periodicamente.
		if (m_board_dirty) {
			redraw();
			m_board_dirty = false;
		} else if (m_phase != Phase::GameOver && frame != 0u && (frame & 7u) == 0u) {
			redraw_live_panels();
		}
	}

	void render(eng::amiga::AmigaBackend&, eng::GameContext& context) {
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	enum class Phase : eng::u8 { Thinking, BookDelay, GameOver };

	[[nodiscard]] Player& current() noexcept {
		return (m_turn == Color::White) ? m_white : m_black;
	}
	[[nodiscard]] Player& opponent() noexcept {
		return (m_turn == Color::White) ? m_black : m_white;
	}

	void start_turn() {
		Player& p = current();
		p.best = kNoMove;
		p.depth = 0u;
		p.nodes = 0u;
		if (p.use_book) {
			const BookProbe probe =
			    probe_opening_book(p.pos, eng::Span<const BookEntry> {g_book, g_book_count});
			if (probe.found) {
				p.best = probe.move;
				m_phase = Phase::BookDelay;
				m_book_delay = kBookDelayFrames;
				return;
			}
		}
		m_phase = Phase::Thinking;
	}

	void commit(Move move, bool from_book) {
		Player& p = current();
		Player& o = opponent();

		const u16 move_number = m_judge.pos.fullmove; // numero antes de la jugada
		char san[12];
		const eng::usize san_len = to_san(m_judge.pos, move, eng::Span<char> {san, sizeof(san)});

		//_texto de la jugada ya formateado para el panel (evita to_san al dibujar).
		p.last_san[0] = '\0';
		append_view(p.last_san, eng::util::StringView {san, san_len}, sizeof(p.last_san));

		// Aplica en las TRES instancias (juez + los dos jugadores).
		Undo u1;
		make_move(m_judge.pos, move, u1);
		m_judge.history.push(m_judge.pos.key);
		Undo u2;
		make_move(p.pos, move, u2);
		p.history.push(p.pos.key);
		Undo u3;
		make_move(o.pos, move, u3);
		o.history.push(o.pos.key);

		// Reloj.
		if (!from_book) {
			p.clock_ms -= kMoveCostMs;
		}
		p.clock_ms += kIncrementMs;
		if (p.clock_ms <= 0) {
			end_game("TIEMPO AGOTADO");
			return;
		}

		// Anuncio del juez.
		build_announce(move_number, san);
		build_comment(p.pos, move);

		// Marca visual de la ultima jugada (casillas de origen y destino).
		m_last_from = move_from(move);
		m_last_to = move_to(move);
		m_has_last = true;

		// Turno.
		m_turn = opposite(m_turn);
		m_judge.move_number = m_judge.pos.fullmove;
		m_board_dirty = true;
		++m_plies;
		if (!m_ready_sent && m_plies >= 2u) {
			m_ready_sent = true;
			eng::debug::mark_ready(g_eng_run_status, 0x12300000u | m_plies);
		}

		const Terminal t = terminal(m_judge.pos, m_judge.history);
		if (t != Terminal::None) {
			end_game(result_text(t));
			return;
		}
		start_turn();
	}

	void build_announce(u16 move_number, const char* san) {
		m_judge.announce[0] = '\0';
		append_uint(m_judge.announce, move_number, sizeof(m_judge.announce));
		append_text(m_judge.announce, m_turn == Color::White ? ". " : "... ",
		            sizeof(m_judge.announce));
		append_text(m_judge.announce, san, sizeof(m_judge.announce));
	}

	void build_comment(const Position& after, Move move) {
		m_judge.comment[0] = '\0';
		const Color just_moved = opposite(to_move(after));
		const DevelopmentFeatures f = extract_development(after);
		const EvalBreakdown e = evaluate_white(after);
		const eng::util::StringView name = book_name_of(after);

		if (in_check(after, to_move(after))) {
			if (terminal(after) == Terminal::Checkmate) {
				append_text(m_judge.comment, "Jaque mate.", sizeof(m_judge.comment));
			} else {
				append_text(m_judge.comment, "Jaque.", sizeof(m_judge.comment));
			}
		} else if (!name.empty()) {
			append_text(m_judge.comment, "Libro: ", sizeof(m_judge.comment));
			append_view(m_judge.comment, name, sizeof(m_judge.comment));
			append_text(m_judge.comment, ".", sizeof(m_judge.comment));
		} else if (f.queen_moved_early[static_cast<eng::u8>(just_moved)]) {
			append_text(m_judge.comment, "Dama prematura: puede ser hostigada.",
			            sizeof(m_judge.comment));
		} else if (f.undeveloped_minors[static_cast<eng::u8>(just_moved)] >= 3u &&
		           f.phase == GamePhase::Opening) {
			append_text(m_judge.comment, "Retraso en el desarrollo de las piezas.",
			            sizeof(m_judge.comment));
		} else if (f.king_in_center[static_cast<eng::u8>(just_moved)]) {
			append_text(m_judge.comment, "Rey en el centro: cuidado con los ataques.",
			            sizeof(m_judge.comment));
		} else if (move_is_capture(move)) {
			append_text(m_judge.comment, "Captura: cambia el material.", sizeof(m_judge.comment));
		} else if (e.pawns < static_cast<Score>(-30)) {
			append_text(m_judge.comment, "Estructura de peones debil.", sizeof(m_judge.comment));
		} else if (move_is_castle_king(move) || move_is_castle_queen(move)) {
			append_text(m_judge.comment, "Enroque: rey a resguardo.", sizeof(m_judge.comment));
		} else {
			append_text(m_judge.comment, "Jugada de desarrollo.", sizeof(m_judge.comment));
		}
	}

	void end_game(const char* reason) {
		m_phase = Phase::GameOver;
		m_judge.result[0] = '\0';
		append_text(m_judge.result, reason, sizeof(m_judge.result));
		++m_plies;
		if (!m_ready_sent) {
			m_ready_sent = true;
			eng::debug::mark_ready(g_eng_run_status, 0x12300000u | m_plies);
		}
	}

	[[nodiscard]] const char* result_text(Terminal t) const {
		switch (t) {
		case Terminal::Checkmate:
			return (to_move(m_judge.pos) == Color::White) ? "JAQUE MATE: ganan las negras"
			                                              : "JAQUE MATE: ganan las blancas";
		case Terminal::Stalemate:
			return "TABLAS: rey ahogado";
		case Terminal::Draw50:
			return "TABLAS: regla de 50 movimientos";
		case Terminal::InsufficientMaterial:
			return "TABLAS: material insuficiente";
		default:
			return "TABLAS";
		}
	}

	// --- Dibujo ---
	void draw_board(eng::u8* planes) {
		for (eng::s32 rank = 0; rank < 8; ++rank) {
			for (eng::s32 file = 0; file < 8; ++file) {
				const eng::s32 x = kBoardX + file * kCell;
				const eng::s32 y = kBoardY + (7 - rank) * kCell;
				const Square sq =
				    make_square(static_cast<eng::u8>(file), static_cast<eng::u8>(rank));
				const bool last = m_has_last && (sq == m_last_from || sq == m_last_to);
				const bool light = ((file + rank) & 1) == 0;
				fill_bytes(planes, x >> 3, y, kCell >> 3, kCell,
				           last ? kColorWarn : (light ? kColorLight : kColorDark));
				const Piece piece = m_judge.pos.board[sq];
				if (piece != kEmptyPiece) {
					const eng::u8 color = (piece_color(piece) == Color::White) ? kColorWhitePiece
					                                                          : kColorBlackPiece;
					draw_char_bytes(planes, x, y + 4, piece_letter(piece_type(piece)), color);
				}
			}
		}
	}

	void draw_player_panel(eng::u8* planes, eng::s32 y, const Player& p, bool white_side) {
		const eng::u8 bg = white_side ? kColorWhitePanel : kColorBlackPanel;
		fill_bytes(planes, kPanelX >> 3, y, 23, 8, bg);
		char line[40];

		line[0] = '\0';
		append_text(line, white_side ? "BLANCAS agresivo" : "NEGRAS posicional", sizeof(line));
		pad_line(line, sizeof(line));
		draw_text_bytes(planes, kPanelX, y + 10, line, kColorText, kPanelChars);

		line[0] = '\0';
		append_text(line, "Prof ", sizeof(line));
		append_uint(line, p.depth, sizeof(line));
		append_text(line, "  Eval ", sizeof(line));
		append_score(line, p.score, sizeof(line));
		pad_line(line, sizeof(line));
		draw_text_bytes(planes, kPanelX, y + 22, line, kColorText, kPanelChars);

		line[0] = '\0';
		append_text(line, "Nodos ", sizeof(line));
		append_uint(line, static_cast<eng::u32>(p.nodes), sizeof(line));
		pad_line(line, sizeof(line));
		draw_text_bytes(planes, kPanelX, y + 34, line, kColorText, kPanelChars);

		line[0] = '\0';
		append_text(line, "Tiempo ", sizeof(line));
		append_uint(line, static_cast<eng::u32>(p.clock_ms / 1000), sizeof(line));
		append_text(line, "s", sizeof(line));
		pad_line(line, sizeof(line));
		draw_text_bytes(planes, kPanelX, y + 46, line, kColorText, kPanelChars);

		line[0] = '\0';
		append_text(line, "Mejor ", sizeof(line));
		if (p.last_san[0] != '\0') {
			append_text(line, p.last_san, sizeof(line));
		} else {
			append_text(line, "...", sizeof(line));
		}
		pad_line(line, sizeof(line));
		draw_text_bytes(planes, kPanelX, y + 58, line, kColorWarn, kPanelChars);

		const bool turn_here = (white_side && m_turn == Color::White) ||
		                       (!white_side && m_turn == Color::Black);
		line[0] = '\0';
		if (turn_here) {
			append_text(line, "PENSANDO...", sizeof(line));
		}
		pad_line(line, sizeof(line));
		draw_text_bytes(planes, kPanelX, y + 70, line, kColorWarn, kPanelChars);
	}

	void draw_judge(eng::u8* planes) {
		const eng::s32 x = kBoardX;
		const eng::s32 y = kBoardY + 8 * kCell + 8;
		draw_text_bytes(planes, x, y, "JUEZ", kColorWarn, kPanelChars);
		draw_text_bytes(planes, x, y + 12, m_judge.announce, kColorText, 16);
		draw_wrapped_bytes(planes, x, y + 24, m_judge.comment, kColorText, 16, 4);
		if (m_phase == Phase::GameOver) {
			draw_text_bytes(planes, x, y + 64, m_judge.result, kColorWarn, 16);
		}
	}

	void redraw() {
		eng::u8* planes = m_scene.bitplanes().data();
		// Sin borrado global: el tablero repinta cada casilla con su color exacto y
		// los paneles reescriben sus lineas completas, asi que cualquier frame
		// intermedio sigue mostrando una pantalla valida (no un fotograma en negro).
		draw_board(planes);
		draw_player_panel(planes, 4, m_white, true);
		draw_player_panel(planes, 128, m_black, false);
		// La zona del juez cambia de longitud: se limpia acotada antes de redibujar.
		fill_bytes(planes, kBoardX >> 3, kBoardY + 8 * kCell + 8, 16, 72, 0u);
		draw_judge(planes);
	}

	/// Refresco barato: solo los dos paneles de jugador (info en vivo). No borra
	/// primero: las lineas se reescriben completas rellenadas a `kPanelChars`, asi
	/// que no queda residuo y una captura a mitad no muestra el panel en negro.
	void redraw_live_panels() {
		eng::u8* planes = m_scene.bitplanes().data();
		draw_player_panel(planes, 4, m_white, true);
		draw_player_panel(planes, 128, m_black, false);
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

	scene::Scene m_scene {};
	bool m_memory_ok = false;
	bool m_scene_ok = false;
	Player m_white {};
	Player m_black {};
	Judge m_judge {};
	Color m_turn = Color::White;
	Phase m_phase = Phase::Thinking;
	eng::u32 m_book_delay = 0u;
	eng::u32 m_plies = 0u;
	bool m_board_dirty = true;
	bool m_ready_sent = false;
	bool m_has_last = false;
	Square m_last_from = 0;
	Square m_last_to = 0;
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::AmigaBackend backend {};
	static ChessMatch game {}; // ~400 KB de TT: en estatica, no en la pila del 68000
	eng::Engine engine {backend, game};
	engine.run_frames_polling(0xffff);

	return 0;
}
