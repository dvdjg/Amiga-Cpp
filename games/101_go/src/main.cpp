// ============================================================================
// Juego 101: GO 9x9 (motor eng::board::go sobre el engine C++23)
// ============================================================================
//
// Consumidor real del motor de Go. El jugador lleva NEGRAS con el joystick y el
// motor juega BLANCAS con el mismo buscador genérico que el ajedrez:
//
//   * REGLAS: `eng::board::GoRules` (grupos/libertades, captura, suicidio, ko simple).
//   * BUSQUEDA: `GoSearcher` (negamax + alpha-beta + iterative deepening +
//     quiescence + TT) elige la jugada blanca a profundidad limitada.
//   * EVALUACION: territorio (flood fill) + capturas + ataris.
//
// Controles: joystick mueve el cursor por las 9x9 intersecciones; FIRE coloca una
// piedra negra (si es legal) y el motor responde. La barra inferior muestra el turno
// y las capturas de cada color.
//
// Display: `StaticEhbScene` (320x256, 6 planos EHB), dibujo directo a bitplanes
// (patron de las demos 060/100). Solo se redibuja cuando hay jugada o se mueve el
// cursor.
//
// Build/run/analyze:
//   bash tools/build/build-demo.sh games/101_go --debug --clean
//   bash tools/run/run-demo.sh games/101_go
//
// Pendiente: pase/dos pases, superko, patrones y pulido visual.

#include <eng/board/rules/go/rules.hpp>
#include <eng/board/persona.hpp>
#include <eng/sim/expression.hpp>
#include <eng/sim/introspection.hpp>
#include <eng/core/types.hpp>
#include <eng/core/util/static_string.hpp>
#include <eng/core/util/text.hpp>
#include <eng/debug/run_status.hpp>
#include <eng/engine.hpp>
#include <eng/graphics/palette32.hpp>
#include <eng/graphics/scene/compose.hpp>
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

namespace scene = eng::graphics::scene;
using namespace eng::board;
using namespace eng::board::go;
using eng::u8;
using eng::u16;
using eng::u32;
using eng::s32;

constexpr eng::u16 kScreenW = 320u;
constexpr eng::u16 kScreenH = 256u;
constexpr eng::u16 kBytesPerRow = 40u;
constexpr eng::u8 kPlanes = 6u;
constexpr eng::u32 kPlaneBytes = 10240u;

constexpr eng::s32 kCell = 20;
constexpr eng::s32 kBoardX = 64;
constexpr eng::s32 kBoardY = 20;

constexpr eng::u8 kColorBg = 0;
constexpr eng::u8 kColorGrid = 2;
constexpr eng::u8 kColorBlackStone = 24;
constexpr eng::u8 kColorWhiteStone = 31;
constexpr eng::u8 kColorCursor = 4;
constexpr eng::u8 kColorText = 20;
constexpr eng::u8 kColorWarn = 26;

GoSearcher g_searcher {};

struct GoGame {
	void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		m_memory_ok = backend.configure_memory({96u * 1024u, 24u * 1024u, 4u * 1024u});

		const eng::Palette32 palette {
			0x000, 0x123, 0x5b3, 0x263, 0xff0, 0xf00, 0x000, 0x000,
			0x210, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
			0x000, 0x000, 0x000, 0x000, 0x0cf, 0x000, 0x000, 0x000,
			0x610, 0x000, 0xf40, 0x000, 0x000, 0x000, 0xff0, 0xfff,
		};
		scene::SceneResources res = scene::planar(320, 256, 6);
		res.mode = scene::SceneMode::Ehb;
		m_scene_ok = scene::compose(m_scene, backend.memory(), res,
				    scene::ocs_a500,
					scene::display(scene::kPal320x256, scene::kBplcon0_Ehb),
					scene::palette(eng::PaletteWords {palette.color, 32u}, 0u, 32u));
		if (!m_memory_ok || !m_scene_ok) {
			eng::debug::mark_failed(g_eng_run_status, 0x00010101u);
			return;
		}

		m_pos = GoRules::initial();
		m_cursor = make_point(4u, 4u);
		m_status[0] = '\0';
		copy_text(m_status, "Tu turno (negras): coloca una piedra con FIRE");

		redraw();
		m_scene.takeover(backend);
		eng::debug::mark_ready(g_eng_run_status, 0x000101FFu);
	}

	void update(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		(void)backend;
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);

		eng::input::InputAggregator input;
		eng::amiga::poll_input(input);
		bool changed = false;
		// Ritmo del humano: si tarda en colocar, el NPC se impacienta.
		if (!terminal_is_over(GoRules::terminal(m_pos))) {
			const bool any_dir = input.pad0.left || input.pad0.right || input.pad0.up || input.pad0.down;
			m_human_wait = any_dir ? 0u : static_cast<u8>(m_human_wait > 250u ? 255u : m_human_wait + 1u);
		}
		m_wait_tells = eng::sim::gestures_for_pace(m_human_wait);
		if (input.pad0.left && point_file(m_cursor) > 0u) {
			m_cursor = static_cast<u8>(m_cursor - 1u);
			changed = true;
		}
		if (input.pad0.right && point_file(m_cursor) + 1u < kSize) {
			m_cursor = static_cast<u8>(m_cursor + 1u);
			changed = true;
		}
		if (input.pad0.up && point_rank(m_cursor) + 1u < kSize) {
			m_cursor = static_cast<u8>(m_cursor + kSize);
			changed = true;
		}
		if (input.pad0.down && point_rank(m_cursor) > 0u) {
			m_cursor = static_cast<u8>(m_cursor - kSize);
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
	void on_fire() {
		if (terminal_is_over(GoRules::terminal(m_pos))) {
			return;
		}
		MoveList legal;
		generate_legal(m_pos, legal);
		Move chosen = kGoPass;
		for (eng::usize i = 0u; i < legal.size(); ++i) {
			if (go_point(legal[i]) == m_cursor) {
				chosen = legal[i];
			}
		}
		if (go_is_pass(chosen)) {
			return; // jugada ilegal en ese punto
		}
		Undo undo;
		GoRules::make(m_pos, chosen, undo);
		ai_move();
		report();
	}

	void ai_move() {
		if (terminal_is_over(GoRules::terminal(m_pos))) {
			return;
		}
		GoSearcher::Limits limits {2u, 200000u};
		const GoSearcher::Result result = g_searcher.search(m_pos, limits);
		if (result.depth == 0u || go_is_pass(result.best_move)) {
			return;
		}
		// Introspeccion: margen entre la mejor y la segunda jugada + tiempo del humano.
		GoSearcher::Line lines[3] {};
		const eng::u32 n_lines = g_searcher.search_multi_pv(m_pos, limits, 3u, eng::Span<GoSearcher::Line> {lines, 3u});
		eng::board::Score line_scores[2] {result.score, result.score};
		if (n_lines >= 2u) {
			line_scores[1] = lines[1].score;
		}
		const eng::u8 time_left = m_human_wait > 200u ? 40u : 200u;
		const eng::sim::DecisionFacts facts = eng::board::facts_from_search(
		    result.score, eng::Span<const eng::board::Score> {line_scores, 2u},
		    static_cast<eng::u8>(legal_count()), false, time_left);
		m_intro = eng::sim::introspect(facts);
		const eng::sim::DecisionFacts after = eng::board::facts_after_opponent(
		    m_prev_eval, result.score, time_left);
		const eng::sim::Introspection opp = eng::sim::introspect(after);
		m_intro.surprise = opp.surprise;
		m_intro.alert = opp.alert;
		m_prev_eval = result.score;
		m_ai_tells = eng::sim::LeakList {};
		if (m_intro.alert > 80u) {
			(void)m_ai_tells.push_back(eng::sim::LeakedGesture {eng::sim::GestureKind::Smirk, m_intro.alert, false});
		}
		if (m_intro.surprise > 80u) {
			(void)m_ai_tells.push_back(eng::sim::LeakedGesture {eng::sim::GestureKind::BrowRaise, m_intro.surprise, false});
		}
		if (m_intro.doubt > 100u) {
			(void)m_ai_tells.push_back(eng::sim::LeakedGesture {eng::sim::GestureKind::Squint, m_intro.doubt, false});
		}
		if (m_intro.confidence > 160u) {
			(void)m_ai_tells.push_back(eng::sim::LeakedGesture {eng::sim::GestureKind::Smile, m_intro.confidence, false});
		}
		Undo undo;
		GoRules::make(m_pos, result.best_move, undo);
	}

	eng::u32 legal_count() const {
		MoveList legal;
		generate_legal(m_pos, legal);
		return static_cast<eng::u32>(legal.size());
	}

	void report() {
		if (terminal_is_over(GoRules::terminal(m_pos))) {
			copy_text(m_status, "Fin: no quedan jugadas legales (cuenta el territorio)");
			return;
		}
		copy_text(m_status, "Tu turno. Capturas  negras:");
		append_int(m_status, m_pos.captures[kBlack]);
		append_text(m_status, " blancas:");
		append_int(m_status, m_pos.captures[kWhite]);
	}

	// --- Dibujo directo a bitplanes ---

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

	void fill_circle(eng::u8* planes, eng::s32 cx, eng::s32 cy, eng::s32 radius, eng::u8 color) {
		for (eng::s32 dy = -radius; dy <= radius; ++dy) {
			for (eng::s32 dx = -radius; dx <= radius; ++dx) {
				if (dx * dx + dy * dy <= radius * radius) {
					put_pixel_exact(planes, cx + dx, cy + dy, color);
				}
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

	/// Cara del NPC (negras) en la cabecera: boca/cejas segun sus gestos y su espera.
	void draw_npc_face(eng::u8* planes) {
		const eng::s32 x = 84;
		const eng::s32 y = 2;
		fill_rect(planes, x, y, 28, 20, kColorCursor);
		fill_rect(planes, x + 1, y + 1, 26, 18, kColorGrid);
		bool smile = false;
		bool frown = false;
		bool yawn = false;
		bool brow_up = false;
		bool squint = false;
		for (eng::usize i = 0u; i < m_ai_tells.size(); ++i) {
			switch (m_ai_tells[i].kind) {
			case eng::sim::GestureKind::Smile: smile = true; break;
			case eng::sim::GestureKind::Squint: squint = true; break;
			case eng::sim::GestureKind::BrowRaise: brow_up = true; break;
			default: break;
			}
		}
		for (eng::usize i = 0u; i < m_wait_tells.size(); ++i) {
			if (m_wait_tells[i].kind == eng::sim::GestureKind::Yawn) { yawn = true; }
			if (m_wait_tells[i].kind == eng::sim::GestureKind::Sigh) { frown = true; }
			if (m_wait_tells[i].kind == eng::sim::GestureKind::Slump) { squint = true; }
		}
		const eng::s32 eh = squint ? 2 : 4;
		fill_rect(planes, x + 6, y + 6, 4, eh, kColorWhiteStone);
		fill_rect(planes, x + 18, y + 6, 4, eh, kColorWhiteStone);
		if (brow_up) {
			fill_rect(planes, x + 5, y + 3, 6, 1, kColorWhiteStone);
			fill_rect(planes, x + 17, y + 3, 6, 1, kColorWhiteStone);
		}
		if (yawn) {
			fill_rect(planes, x + 11, y + 12, 6, 4, kColorWhiteStone);
		} else if (smile) {
			fill_rect(planes, x + 8, y + 15, 12, 1, kColorWhiteStone);
		} else if (frown) {
			fill_rect(planes, x + 8, y + 16, 12, 1, kColorWarn);
		} else {
			fill_rect(planes, x + 10, y + 15, 8, 1, kColorGrid);
		}
	}

	void redraw() {
		eng::u8* planes = m_scene.bitplanes().data();
		for (eng::u32 i = 0u; i < kPlaneBytes * kPlanes; ++i) {
			planes[i] = 0u;
		}

		// Rejilla.
		draw_text(planes, 8, 4, "GO 9x9", kColorWhiteStone);
		draw_npc_face(planes);
		for (eng::s32 i = 0; i < static_cast<eng::s32>(kSize); ++i) {
			const eng::s32 x = kBoardX + i * kCell;
			const eng::s32 y = kBoardY + i * kCell;
			fill_rect(planes, kBoardX, y, (kSize - 1) * kCell + 1, 1, kColorGrid);
			fill_rect(planes, x, kBoardY, 1, (kSize - 1) * kCell + 1, kColorGrid);
		}

		// Piedras.
		for (u8 point = 0u; point < kCells; ++point) {
			const u8 stone = m_pos.board[point];
			if (stone == kEmpty) {
				continue;
			}
			const eng::s32 cx = kBoardX + static_cast<eng::s32>(point_file(point)) * kCell;
			const eng::s32 cy = kBoardY + static_cast<eng::s32>(kSize - 1u - point_rank(point)) * kCell;
			fill_circle(planes, cx, cy, 7, (stone == kBlack) ? kColorBlackStone : kColorWhiteStone);
		}

		// Cursor.
		const eng::s32 cx = kBoardX + static_cast<eng::s32>(point_file(m_cursor)) * kCell;
		const eng::s32 cy = kBoardY + static_cast<eng::s32>(kSize - 1u - point_rank(m_cursor)) * kCell;
		fill_circle(planes, cx, cy, 9, kColorCursor);

		// Estado.
		draw_text(planes, 8, kBoardY + static_cast<eng::s32>(kSize) * kCell + 10, m_status,
		          terminal_is_over(GoRules::terminal(m_pos)) ? kColorWarn : kColorText);
	}

	static void copy_text(char* dst, const char* src) {
		eng::usize i = 0u;
		while (src[i] != '\0' && i < kStatusCap - 1u) {
			dst[i] = src[i];
			++i;
		}
		dst[i] = '\0';
	}

	static void append_text(char* dst, const char* src) {
		eng::usize i = 0u;
		while (dst[i] != '\0') {
			++i;
		}
		eng::usize k = 0u;
		while (src[k] != '\0' && i < kStatusCap - 1u) {
			dst[i++] = src[k++];
		}
		dst[i] = '\0';
	}

	static void append_int(char* dst, unsigned value) {
		eng::util::StaticString<8> digits;
		(void)eng::util::to_chars_u32(digits, static_cast<eng::u32>(value));
		append_text(dst, digits.c_str());
	}

	static constexpr eng::usize kStatusCap = 120;

	scene::Scene m_scene {};
	bool m_memory_ok = false;
	bool m_scene_ok = false;
	Position m_pos {};
	u8 m_cursor = make_point(4u, 4u);
	bool m_fire_held = false;
	char m_status[kStatusCap] {};
	eng::sim::Introspection m_intro {};
	eng::sim::LeakList m_ai_tells {};
	eng::sim::LeakList m_wait_tells {};
	eng::board::Score m_prev_eval = 0;
	u8 m_human_wait = 0u;
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::MinimalBackend backend {};
	GoGame game {};
	eng::Engine engine {backend, game};
	engine.run_frames_polling(0xffff);

	return 0;
}
