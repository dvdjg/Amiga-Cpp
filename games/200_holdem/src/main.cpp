// ============================================================================
// Juego 200: POKER Texas Hold'em No-Limit (motor eng::cards sobre el engine C++23)
// ============================================================================
//
// Primer consumidor real de `eng::cards`. El jugador (asiento 0) juega contra dos
// bots CPU con el joystick:
//
//   * REGLAS: `eng::cards::Table` (ciegas, calles, acciones legales, side pots,
//     showdown) en No-Limit.
//   * IA: `decide_with_plan` con perfil `N20` (heuristica preflop + fuerza de mano,
//     sin Monte Carlo) para que quepa sobradamente en un A500.
//   * DISPLAY: escena EHB (320x256) sobre `scene::compose`, rasterizada por CPU (patron de la demo 060).
//
// Controles: izquierda/derecha eligen accion; FIRE la confirma. Al terminar la mano,
// FIRE reparte la siguiente.
//
// Build/run/analyze (mismos wrappers que una demo):
//   bash tools/build/build-demo.sh games/200_holdem --debug --clean
//   bash tools/run/run-demo.sh games/200_holdem
//
// Verificacion: build -> run -> analyze (pendiente de cierre en emulador).

#include <eng/cards/ai/bot.hpp>
#include <eng/cards/core/budget.hpp>
#include <eng/cards/ai/persona_bot.hpp>
#include <eng/cards/rules/texas_holdem.hpp>
#include <eng/core/random.hpp>
#include <eng/api/api.hpp>
#include <eng/core/util/static_string.hpp>
#include <eng/core/util/text.hpp>
#include <eng/graphics/font8.hpp>
#include <eng/platform/amiga_minimal.hpp>
#include <eng/platform/input_poll.hpp>
#include <eng/sim/persona.hpp>
#include <eng/sim/psyche.hpp>
#include <eng/sim/body.hpp>

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
using namespace eng::cards;
using eng::s32;
using eng::u8;
using eng::u16;
using eng::u32;

constexpr eng::u16 kScreenW = 320u;
constexpr eng::u16 kScreenH = 256u;
constexpr eng::u16 kBytesPerRow = 40u;
constexpr eng::u8 kPlanes = 6u;
constexpr eng::u32 kPlaneBytes = 10240u;

constexpr eng::u8 kColorFelt = 2;
constexpr eng::u8 kColorCard = 6;
constexpr eng::u8 kColorText = 31;
constexpr eng::u8 kColorDim = 12;
constexpr eng::u8 kColorSel = 26;
constexpr eng::u8 kColorWarn = 30;

constexpr u8 kSeats = 3u;
constexpr u8 kHuman = 0u;
constexpr s32 kStack = 1000;
constexpr s32 kSb = 5;
constexpr s32 kBigBlind = 10;

struct HoldemGame {
	void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		m_memory_ok = backend.configure_memory({96u * 1024u, 16u * 1024u, 4u * 1024u});

		const eng::Palette32 palette {
			0x000, 0x123, 0x0a0, 0x030, 0x0f0, 0xfff, 0x666, 0x000,
			0x111, 0x000, 0x000, 0x000, 0x777, 0x000, 0x000, 0x000,
			0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
			0x000, 0x000, 0xf80, 0x000, 0x000, 0x000, 0xff0, 0xfff,
		};
		scene::SceneResources res = scene::planar(320, 256, 6);
		res.mode = scene::SceneMode::Ehb;
		m_scene_ok = scene::compose(m_scene, backend.memory(), res,
				    scene::ocs_a500,
					scene::display(res),
					scene::palette(eng::PaletteWords {palette.color, 32u}, 0u, 32u));
		if (!m_memory_ok || !m_scene_ok) {
			eng::debug::mark_failed(g_eng_run_status, 0x00020001u);
			return;
		}

		m_plan = card_profile_plan(CardProfile::N20);
		// Cada asiento es un personaje con arquetipo propio; el humano tambien tiene el suyo.
		eng::Xoroshiro64pp persona_rng {0xbeefu, 0x1234u};
		m_personas[kHuman] = eng::sim::make_persona(eng::sim::Archetype::Flematico, persona_rng, 12u);
		m_personas[1] = eng::sim::make_persona(eng::sim::Archetype::Pardillo, persona_rng, 12u);
		m_personas[2] = eng::sim::make_persona(eng::sim::Archetype::Engreido, persona_rng, 12u);
		for (u8 i = 0u; i < kSeats; ++i) {
			m_psyche[i] = eng::sim::initial_psyche(m_personas[i]);
			m_tells[i].clear();
			m_last_action[i] = ActionType::Check;
		}
		m_model.reset();

		new_hand();
		m_scene.takeover(backend);
		eng::debug::mark_ready(g_eng_run_status, 0x000200FFu);
	}

	void update(eng::amiga::MinimalBackend&, eng::GameContext& context) {
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
		++m_frame_count;
		if (!m_memory_ok || !m_scene_ok) {
			return;
		}
		eng::input::InputAggregator input;
		eng::amiga::poll_input(input);
		bool changed = false;

		if (m_table.hand_over) {
			if (input.pad0.fire && !m_fire_held) {
				new_hand();
				changed = true;
			}
			m_fire_held = input.pad0.fire;
			if (changed) {
				redraw();
			}
			return;
		}

		if (m_table.to_act == kHuman) {
			refresh_legal();
			if (m_legal_count > 0u) {
				if (input.pad0.right) {
					m_menu = static_cast<u8>((m_menu + 1u) % m_legal_count);
					changed = true;
				}
				if (input.pad0.left) {
					m_menu = static_cast<u8>((m_menu + m_legal_count - 1u) % m_legal_count);
					changed = true;
				}
				if (input.pad0.fire && !m_fire_held) {
					emit_and_record_tells(kHuman);
					apply_action(m_table, m_legal[m_menu]);
					m_last_action[kHuman] = m_legal[m_menu].type;
					m_menu = 0u;
					run_cpu_turns();
					changed = true;
				}
			}
		} else {
			run_cpu_turns();
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
	void new_hand() {
		// Cierra la mano anterior: evoluciona la psique y la lectura de la mesa.
		if (m_hand_started) {
			settle_psyche();
		}
		start_hand(m_table, m_rng, kSeats, kStack, kSb, kBigBlind, m_button);
		m_button = static_cast<u8>((m_button + 1u) % kSeats);
		m_menu = 0u;
		m_legal_count = 0u;
		m_hand_started = true;
		if (m_table.to_act != kHuman) {
			run_cpu_turns();
		}
		redraw();
	}

	void refresh_legal() {
		m_legal_count = legal_actions(m_table, eng::Span<Action> {m_legal, 12u});
		if (m_menu >= m_legal_count) {
			m_menu = 0u;
		}
	}

	void run_cpu_turns() {
		u32 guard = 0u;
		while (!m_table.hand_over && m_table.to_act != kHuman && guard < 64u) {
			const u8 actor = m_table.to_act;
			if (actor == kNoSeat) {
				break;
			}
			// Decision modulada por la persona y su estado; la mesa lee sus tells.
			const Action action = decide_with_persona(m_table, actor, m_personas[actor],
			                                          m_psyche[actor], m_plan,
			                                          BotStyle::Balanced, m_opp_model, m_rng);
			emit_and_record_tells(actor);
			apply_action(m_table, action);
			m_opp_model.observe(actor, action.type, m_table.street);
			m_last_action[actor] = action.type;
			++guard;
		}
	}

	/// Calcula y guarda los tells del asiento tras decidir (para que la mesa los lea).
	void emit_and_record_tells(u8 actor) {
		// El afecto refleja el resultado reciente; se aproxima con la racha/tilt del estado.
		eng::sim::Mind mind {};
		mind.emotions.fear = m_psyche[actor].tension;
		mind.emotions.anger = m_psyche[actor].tilt;
		mind.emotions.joy = m_psyche[actor].mood;
		mind.emotions.hatred = m_psyche[actor].tilt / 2u;
		m_tells[actor] = emit_tells(mind, m_psyche[actor], m_personas[actor]);
	}

	/// Cierra la mano: reparte el resultado psicologico a cada asiento segun si gano o no.
	void settle_psyche() {
		// Ganador del bote (mayor stack delta no es fiable; usamos el ganador de la mesa).
		for (u8 i = 0u; i < kSeats; ++i) {
			eng::sim::Mind mind {};
			mind.emotions.fear = m_psyche[i].tension;
			mind.emotions.anger = m_psyche[i].tilt;
			mind.emotions.joy = m_psyche[i].mood;
			const bool won = (m_table.winner_seat == i) ||
			                 (m_table.street == Street::Showdown && m_table.seats[i].stack > kStack);
			const eng::sim::TableEventKind ev = won
			    ? eng::sim::TableEventKind::WonShowdown
			    : eng::sim::TableEventKind::LostShowdown;
			eng::sim::psyche_observe(m_psyche[i], mind, ev, m_personas[i]);
			eng::sim::psyche_update(m_psyche[i], mind, m_personas[i]);
			// La mesa aprende: los tells vistos se etiquetan con el resultado (fuerte = gano).
			read_showdown(m_model, i, won, m_tells[i]);
		}
	}

	void apply_action(Table& t, const Action& action) {
		eng::cards::apply_action(t, action);
	}

	// --- Dibujo directo a bitplanes (patron de la demo 060) ---

	void put_pixel(eng::u8* planes, eng::s32 x, eng::s32 y, eng::u8 color) {
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
				put_pixel(planes, x + px, y + py, color);
			}
		}
	}

	void glyph(eng::u8* planes, eng::s32 x, eng::s32 y, char c, eng::u8 color) {
		for (eng::u8 row = 0; row < eng::Font8::kRows; ++row) {
			const eng::u8 g = eng::Font8::row(static_cast<eng::u16>(static_cast<eng::u8>(c)), row);
			for (eng::u8 k = 0; k < 8u; ++k) {
				if ((g & (1u << k)) != 0u) {
					put_pixel(planes, x + k, y + row, color);
				}
			}
		}
	}

	void text(eng::u8* planes, eng::s32 x, eng::s32 y, const char* s, eng::u8 color) {
		for (eng::s32 i = 0; s[i] != '\0'; ++i) {
			glyph(planes, x, y, s[i], color);
			x += 8;
		}
	}

	static char rank_char(Card card) {
		if (!card_valid(card)) {
			return '?';
		}
		constexpr char kRanks[13] = {'2', '3', '4', '5', '6', '7', '8', '9', 'T', 'J', 'Q', 'K', 'A'};
		return kRanks[card >> 2u];
	}

	static char suit_char(Card card) {
		if (!card_valid(card)) {
			return '?';
		}
		constexpr char kSuits[4] = {'c', 'd', 'h', 's'};
		return kSuits[card & 0x03u];
	}

	void draw_card(eng::u8* planes, eng::s32 x, eng::s32 y, Card card, bool hidden) {
		fill_rect(planes, x, y, 26, 30, kColorCard);
		if (hidden) {
			text(planes, x + 5, y + 11, "?", kColorDim);
			return;
		}
		if (!card_valid(card)) {
			return;
		}
		char r[2] {rank_char(card), '\0'};
		char s[2] {suit_char(card), '\0'};
		text(planes, x + 9, y + 4, r, kColorText);
		text(planes, x + 9, y + 16, s, (card & 0x03u) >= 2u ? kColorWarn : kColorText);
	}

	void draw_number(eng::u8* planes, eng::s32 x, eng::s32 y, eng::s32 value, eng::u8 color) {
		eng::util::StaticString<16> s;
		(void)eng::util::to_chars_s32(s, value);
		text(planes, x, y, s.c_str(), color);
	}

	/// Busca la intensidad de un gesto en la lista de fugas del asiento.
	[[nodiscard]] u8 tell_intensity(u8 seat, eng::sim::GestureKind g) const {
		for (eng::usize i = 0u; i < m_tells[seat].size(); ++i) {
			if (m_tells[seat][i].kind == g) {
				return m_tells[seat][i].intensity;
			}
		}
		return 0u;
	}

	/// Postura combinada de los tells del asiento (suma de `pose_from_gesture` por gesto).
	[[nodiscard]] eng::sim::BodyPose tell_pose(u8 seat) const {
		eng::sim::BodyPose total {};
		for (eng::usize i = 0u; i < m_tells[seat].size(); ++i) {
			total = eng::sim::pose_add(
				total, eng::sim::pose_from_gesture(m_tells[seat][i].kind,
				                                   m_tells[seat][i].intensity));
		}
		return total;
	}

	/// Dibuja un avatar (cara + postura) cuyo gesto refleja los tells del asiento: la boca
	/// sonríe o se frunce, las cejas suben o bajan, tiembla si está muy nervioso y **se
	/// inclina/retrocede** según la postura combinada.
	void draw_avatar(eng::u8* planes, eng::s32 x, eng::s32 y, u8 seat, bool active) {
		const eng::sim::BodyPose pose = tell_pose(seat);
		// La postura desplaza el conjunto (lean = hacia delante, recoil = hacia atrás).
		const eng::s32 shift_x = pose.lean / 20 - pose.recoil / 20;
		x += shift_x;
		y += pose.crouch / 12;

		const eng::u8 face = active ? kColorSel : kColorCard;
		fill_rect(planes, x, y, 34, 40, face);
		fill_rect(planes, x + 2, y + 2, 30, 36, kColorFelt);
		// Ojos (pupilas mas grandes con pupil_dilate/wide_eyes = sorpresa/interes).
		const u8 wide = tell_intensity(seat, eng::sim::GestureKind::WideEyes);
		const u8 dilate = tell_intensity(seat, eng::sim::GestureKind::PupilDilate);
		const eng::s32 eye = (wide > 0u || dilate > 0u) ? 7 : 5;
		fill_rect(planes, x + 8, y + 12 - (eye - 5) / 2, eye, eye, kColorText);
		fill_rect(planes, x + 21, y + 12 - (eye - 5) / 2, eye, eye, kColorText);
		// Cejas segun brow_raise / brow_furrow.
		const u8 raise = tell_intensity(seat, eng::sim::GestureKind::BrowRaise);
		const u8 furrow = tell_intensity(seat, eng::sim::GestureKind::BrowFurrow);
		const eng::s32 brow_dy = (raise > furrow) ? -3 : (furrow > raise ? 2 : 0);
		fill_rect(planes, x + 7, y + 9 + brow_dy, 7, 2, kColorText);
		fill_rect(planes, x + 20, y + 9 + brow_dy, 7, 2, kColorText);
		// Boca segun smile / grimace / smirk / jaw_drop.
		const u8 smile = tell_intensity(seat, eng::sim::GestureKind::Smile);
		const u8 grimace = tell_intensity(seat, eng::sim::GestureKind::Grimace);
		const u8 smirk = tell_intensity(seat, eng::sim::GestureKind::Smirk);
		const u8 jaw = tell_intensity(seat, eng::sim::GestureKind::JawDrop);
		if (jaw > 40u) {
			fill_rect(planes, x + 13, y + 25, 8, 6, kColorText); // boca abierta
		} else if (smile >= grimace && smile >= smirk && smile > 0u) {
			fill_rect(planes, x + 10, y + 26, 14, 2, kColorText);
			fill_rect(planes, x + 9, y + 24, 2, 2, kColorText);
			fill_rect(planes, x + 23, y + 24, 2, 2, kColorText);
		} else if (grimace > 0u) {
			fill_rect(planes, x + 10, y + 28, 14, 2, kColorWarn);
			fill_rect(planes, x + 9, y + 30, 2, 2, kColorWarn);
			fill_rect(planes, x + 23, y + 30, 2, 2, kColorWarn);
		} else if (smirk > 0u) {
			fill_rect(planes, x + 12, y + 26, 12, 2, kColorText);
			fill_rect(planes, x + 22, y + 24, 2, 2, kColorText);
		} else {
			fill_rect(planes, x + 12, y + 27, 10, 2, kColorDim);
		}
		// Temblor: un temblor de mano alto marca la cara con un borde parpadeante.
		const u8 tremor = tell_intensity(seat, eng::sim::GestureKind::HandTremor);
		if (tremor > 40u && ((m_frame_count & 1u) == 0u)) {
			fill_rect(planes, x, y, 34, 2, kColorWarn);
			fill_rect(planes, x, y + 38, 34, 2, kColorWarn);
		}
		// Barra de estado: confianza (verde) y presion/tilt (rojo) del asiento.
		const eng::u8 conf = m_psyche[seat].confidence;
		const eng::u8 tilt = m_psyche[seat].tilt;
		fill_rect(planes, x, y + 42, 34, 3, kColorDim);
		fill_rect(planes, x, y + 42, (static_cast<eng::s32>(conf) * 34) / 100, 3,
		          tilt > 120u ? kColorWarn : kColorSel);
	}

	void redraw() {
		eng::u8* planes = m_scene.bitplanes().data();
		for (eng::u32 i = 0u; i < kPlaneBytes * kPlanes; ++i) {
			planes[i] = 0u;
		}
		fill_rect(planes, 0, 0, kScreenW, kScreenH, kColorFelt);

		text(planes, 8, 4, "TEXAS HOLD'EM  N20", kColorText);

		// Cartas comunitarias.
		for (u8 i = 0u; i < kBoardCards; ++i) {
			draw_card(planes, 24 + static_cast<eng::s32>(i) * 34, 20, m_table.board[i],
			          i >= m_table.board_count);
		}

		// Bote y apuesta viva.
		text(planes, 12, 58, "BOTE", kColorWarn);
		draw_number(planes, 52, 58, m_table.pot, kColorWarn);
		text(planes, 150, 58, "APUESTA", kColorDim);
		draw_number(planes, 214, 58, m_table.current_bet, kColorText);

		// Stacks CPU y cartas tapadas.
		text(planes, 12, 80, "CPU1:", kColorText);
		draw_number(planes, 52, 80, m_table.seats[1].stack, kColorText);
		text(planes, 160, 80, "CPU2:", kColorText);
		draw_number(planes, 200, 80, m_table.seats[2].stack, kColorText);
		draw_card(planes, 12, 96, m_table.seats[1].hole[0], true);
		draw_card(planes, 42, 96, m_table.seats[1].hole[1], true);
		draw_card(planes, 200, 96, m_table.seats[2].hole[0], true);
		draw_card(planes, 230, 96, m_table.seats[2].hole[1], true);

		// Avatares de los jugadores CPU (la cara refleja sus tells).
		draw_avatar(planes, 268, 96, 1u, m_table.to_act == 1u);
		draw_avatar(planes, 268, 140, 2u, m_table.to_act == 2u);
		text(planes, 268, 84, "CPU", kColorDim);

		// Cartas del jugador.
		text(planes, 12, 150, "TU:", kColorText);
		draw_number(planes, 100, 150, m_table.seats[kHuman].stack, kColorText);
		const bool folded = m_table.seats[kHuman].status == SeatStatus::Folded;
		draw_card(planes, 12, 164, m_table.seats[kHuman].hole[0], folded);
		draw_card(planes, 42, 164, m_table.seats[kHuman].hole[1], folded);

		// Menu de acciones.
		if (m_table.hand_over) {
			text(planes, 12, 216, "MANO TERMINADA - FIRE SIGUE", kColorSel);
		} else if (m_table.to_act == kHuman && m_legal_count > 0u) {
			eng::s32 x = 12;
			for (u8 i = 0u; i < m_legal_count; ++i) {
				const char* name = "?";
				switch (m_legal[i].type) {
				case ActionType::Fold:
					name = "PASAR";
					break;
				case ActionType::Check:
					name = "PASO";
					break;
				case ActionType::Call:
					name = "IGUALAR";
					break;
				case ActionType::Raise:
					name = "SUBIR";
					break;
				case ActionType::AllIn:
					name = "ALL-IN";
					break;
				default:
					break;
				}
				text(planes, x, 216, name, i == m_menu ? kColorSel : kColorDim);
				x += 8 * 8;
			}
		} else {
			text(planes, 12, 216, "TURNO DE LA CPU...", kColorDim);
		}
	}

	scene::Scene m_scene {};
	Table m_table {};
	CardPlan m_plan {};
	eng::sim::Persona m_personas[kMaxSeats] {};
	eng::sim::PsycheState m_psyche[kMaxSeats] {};
	eng::sim::LeakList m_tells[kMaxSeats] {};
	eng::sim::ReadModel<kSeats, kPokerGestureCount> m_model {};
	OpponentModel m_opp_model {};
	ActionType m_last_action[kMaxSeats] {};
	eng::Xoroshiro64pp m_rng {0x200u, 0xa11u};
	Action m_legal[12] {};
	u8 m_legal_count = 0u;
	u8 m_menu = 0u;
	u8 m_button = 0u;
	u32 m_frame_count = 0u;
	bool m_fire_held = false;
	bool m_hand_started = false;
	bool m_memory_ok = false;
	bool m_scene_ok = false;
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::MinimalBackend backend {};
	HoldemGame game {};
	eng::Engine engine {backend, game};
	engine.run_frames_polling(0xffff);

	return 0;
}
