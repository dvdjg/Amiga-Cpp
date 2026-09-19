// ============================================================================
// Demo 124: benchmark del motor de naipes (`eng::cards`) en Amiga.
// ============================================================================
//
// Mide **unidades/s** con el contador TOD de la CIA-A (50 Hz PAL), sin depender de
// que la unidad quepa en un frame: repite "1 mano + N muestras de equity" hasta
// agotar un presupuesto de tiempo emulado y calcula la tasa real. El perfil se elige
// en compilación con `-DCARDS_BENCH_PROFILE=<0..4>` (0=N20 … 4=N512).
//
// Publica en `g_eng_run_status.detail`:
//   bits 31..16  unidades/s
//   bits 15..0   ms por unidad
//
// y lo dibuja en pantalla. Cambiar el target (`TARGET_MACHINE`) compararia CPU.
//
// Build/run/analyze (mismos wrappers que una demo):
//   bash tools/build/build-demo.sh demos/amiga/124_cards_bench --release --clean
//   bash tools/run/run-demo.sh demos/amiga/124_cards_bench --warp
//
// Verificacion: build -> run -> READY OK (evidencia en el README).

#include <eng/core/random.hpp>
#include <eng/core/types.hpp>
#include <eng/core/util/static_string.hpp>
#include <eng/core/util/text.hpp>
#include <eng/debug/run_status.hpp>
#include <eng/engine.hpp>
#include <eng/platform/amiga_minimal.hpp>

#include <eng/cards/ai/bot.hpp>
#include <eng/cards/core/budget.hpp>
#include <eng/cards/eval/equity.hpp>
#include <eng/cards/sim/session.hpp>

#include <proto/exec.h>
#include <exec/execbase.h>

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

using namespace eng::cards;

// Perfil medido, seleccionable en compilación con -DCARDS_BENCH_PROFILE=<0..4>.
#ifndef CARDS_BENCH_PROFILE
#define CARDS_BENCH_PROFILE 0 // 0=N20, 1=N64, 2=N128, 3=N256, 4=N512
#endif
constexpr CardProfile kProfile = static_cast<CardProfile>(CARDS_BENCH_PROFILE);

constexpr eng::u16 kTableSamples = 8u;
constexpr eng::u8 kSeats = 2u;          // heads-up: mano corta
constexpr eng::u16 kEquityBatch = 1u;   // muestras de equity dentro de la unidad
constexpr eng::u32 kTargetTicks = 500u; // presupuesto de medida: 10 s emulados (50 Hz)
constexpr eng::u32 kMaxUnits = 200000u;

void append(char* dst, const char* src) {
	while (*dst != '\0') {
		++dst;
	}
	while (*src != '\0') {
		*dst++ = *src++;
	}
	*dst = '\0';
}

struct CardsBench {
	void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		m_memory_ok = backend.configure_memory({4096, 4096, 1024});

		eng::Xoroshiro64pp table_rng {0x1234u, 0x5678u};
		build_preflop_table(m_table, table_rng, kTableSamples);

		m_plan = card_profile_plan(kProfile);
		m_cfg.seats = kSeats;
		m_cfg.starting_stack = 1000;
		m_cfg.small_blind = 5;
		m_cfg.big_blind = 10;
		m_cfg.hands = 1u;
		m_cfg.seed = 1u;
		for (eng::u8 i = 0u; i < kMaxSeats; ++i) {
			m_cfg.styles[i] =
			    (i % 2u) == 0u ? BotStyle::TightAggressive : BotStyle::LoosePassive;
		}

		// Ventana de medida adaptativa por TOD (50 Hz): se repite la unidad hasta
		// agotar `kTargetTicks` o `kMaxUnits`, lo que llegue antes.
		const eng::u32 start = backend.cia_tod_ticks();
		eng::u32 units = 0u;
		eng::u32 elapsed = 0u;
		while (units < kMaxUnits) {
			run_unit();
			++units;
			elapsed = (backend.cia_tod_ticks() - start) & 0x00ffffffu;
			if (elapsed >= kTargetTicks) {
				break;
			}
		}
		m_units = units;
		m_elapsed_ticks = elapsed;
		m_units_per_s = elapsed == 0u ? 0u : div32(units * 50u, elapsed);
		// ms por unidad = elapsed_ticks * 20 / units (1 tick = 20 ms; 20 = 16+4).
		const eng::u32 per_unit_ms = units == 0u
		                                 ? 0u
		                                 : div32((elapsed << 4u) + (elapsed << 2u), units);
		g_eng_run_status.detail =
		    (m_units_per_s << 16u) | (per_unit_ms > 0xffffu ? 0xffffu : per_unit_ms);
		eng::debug::mark_ready(g_eng_run_status, g_eng_run_status.detail);
	}

	void update(eng::amiga::MinimalBackend&, eng::GameContext& context) {
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
	}

	void render(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		auto& debug = backend.debug();
		debug.clear();
		debug.filled_rect(40, 40, 560, 200, 0x00081018);
		debug.rect(40, 40, 560, 200, 0x00ffffff);
		debug.text(60, 60, "cards bench - eng::cards on Amiga", 0x00ffffff);

		auto number = [](eng::u32 v) {
			static eng::util::StaticString<16> s;
			s.clear();
			(void)eng::util::to_chars_u32(s, v);
			return s.c_str();
		};
		char line[56];

		line[0] = '\0';
		append(line, "perfil: ");
		append(line, number(static_cast<eng::u32>(kProfile)));
		debug.text(60, 90, line, 0x0080ff80);

		line[0] = '\0';
		append(line, "unidades/s: ");
		append(line, number(m_units_per_s));
		debug.text(60, 120, line, 0x00ffff00);

		line[0] = '\0';
		append(line, "unidades: ");
		append(line, number(m_units));
		debug.text(60, 150, line, 0x00ffc040);

		eng::debug::probe_when_ready(g_eng_run_status, 0u);
	}

private:
	void run_unit() {
		SessionConfig run_cfg = m_cfg;
		run_cfg.seed = m_seed++;
		SessionStats stats {};
		run_session(run_cfg, m_plan, stats, nullptr);
		m_hands += stats.hands_played;

		const eng::u8 hole[2] {make_card(Rank::Ace, Suit::Spades),
		                       make_card(Rank::King, Suit::Spades)};
		const eng::u8 board[3] {make_card(Rank::Two, Suit::Clubs),
		                        make_card(Rank::Seven, Suit::Hearts),
		                        make_card(Rank::Nine, Suit::Diamonds)};
		(void)equity_vs_random(eng::Span<const eng::u8> {hole, 2u},
		                       eng::Span<const eng::u8> {board, 3u}, 1u, kEquityBatch, m_rng);
	}

	PreflopTable m_table {};
	CardPlan m_plan {};
	SessionConfig m_cfg {};
	eng::Xoroshiro64pp m_rng {0x9e37u, 0x79b9u};
	eng::u32 m_seed = 1u;
	eng::u32 m_hands = 0u;
	eng::u32 m_units = 0u;
	eng::u32 m_elapsed_ticks = 0u;
	eng::u32 m_units_per_s = 0u;
	bool m_memory_ok = false;
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::MinimalBackend backend {};
	CardsBench game {};
	eng::Engine engine {backend, game};
	engine.run_frames_polling(0xffff);

	return 0;
}
