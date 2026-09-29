// ============================================================================
// Juego 300: SIM WORLD (ecosistema `eng::sim` sobre el engine C++23)
// ============================================================================
//
// Primer consumidor **real** de `eng::sim` (mas alla del benchmark `001_sim_bench`):
// el jugador observa un ecosistema vivo y lo sigue con la camara.
//
//   * MUNDO: `SimWorld` con criaturas que comen, se mueven y planifican (GOAP con
//     presupuesto y reutilizacion de sufijo via `plan_tick`).
//   * CAMARA = LOD: la camara es una **room**; con izquierda/derecha se cambia. Cada
//     frame se marca `realize_room(camara, N)`, de modo que **solo lo cercano a la
//     camara planifica**: el LOD decide quien gasta CPU (igual que en un juego real).
//   * HUD: poblacion, planes activos, expansiones de la ultima busqueda y frames.
//
// Controles: izquierda/derecha mueven la camara; fuego anade una criatura.
//
// Build/run/analyze (mismos wrappers que una demo):
//   bash tools/build/build-demo.sh games/300_sim_world --debug --clean
//   bash tools/run/run-demo.sh games/300_sim_world --warp
//
// Verificacion: build -> run -> READY OK.

#include <eng/api/api.hpp>
#include <eng/core/math/random.hpp>
#include <eng/core/util/static_string.hpp>
#include <eng/core/util/text.hpp>
#include <eng/platform/amiga/backend.hpp>
#include <eng/platform/amiga/input_poll.hpp>

#include <eng/sim/domain.hpp>
#include <eng/sim/planner.hpp>
#include <eng/sim/world.hpp>

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

using namespace eng::sim;

using World = SimWorld<SimTraits, 16, 4, 4, 8>;
constexpr eng::u8 kRooms = 4u;
constexpr eng::u8 kRealize = 8u;     // criaturas realizadas por room (LOD)
constexpr eng::u16 kPlanBudget = 32u;

// Memoria **estatica** (no pila): mundo (~24 KB), planificador y generador.
World g_world {};
eng::Xoroshiro64pp g_rng {0x1234u, 0x5678u};

void append(char* dst, const char* src) {
	while (*dst != '\0') {
		++dst;
	}
	while (*src != '\0') {
		*dst++ = *src++;
	}
	*dst = '\0';
}

struct SimWorldGame {
	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		m_memory_ok = backend.configure_memory({4096, 4096, 1024});

		for (eng::u8 i = 1u; i < kRooms; ++i) {
			g_world.link_rooms(static_cast<RoomId>(i - 1u), static_cast<RoomId>(i));
		}
		for (eng::u8 i = 0u; i < 12u; ++i) {
			(void)spawn_one(i);
		}
		eng::debug::mark_ready(g_eng_run_status, 0x000300FFu);
	}

	void update(eng::amiga::AmigaBackend&, eng::GameContext& context) {
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);

		eng::input::InputAggregator input;
		eng::amiga::poll_input(input);

		// Camara = room del LOD.
		if (input.pad0.left && m_room > 0u) {
			--m_room;
		}
		if (input.pad0.right && m_room + 1u < kRooms) {
			++m_room;
		}
		if (input.pad0.fire && !m_fire_held) {
			(void)spawn_one(static_cast<eng::u8>(context.frame.frame_index));
		}
		m_fire_held = input.pad0.fire;

		// **LOD**: solo lo cercano a la camara se simula con detalle y planifica.
		g_world.realize_room(m_room, kRealize);
		g_world.tick_realized(g_rng);
		g_world.tick_abstract(g_rng);

		// Planificacion de las criaturas realizadas (presupuesto + reutilizacion).
		PlanParams params {};
		params.budget = kPlanBudget;
		params.replan_interval = 60u;
		const SimGoap::Goal goal = ConstructionDomain::goal(false, true);
		const auto acts = ConstructionDomain::actions();
		const SimGoap::State start = start_state(SimInventory {});
		m_expansions = 0u;
		for (eng::usize i = 0u; i < g_world.creature_count(); ++i) {
			const EntityId id = g_world.creature(i).id;
			if (g_world.plan_tick(id, start, goal, acts.span(),
					      static_cast<eng::u16>(context.frame.frame_index), params)) {
				++m_planned;
			}
			m_expansions += static_cast<eng::u32>(g_world.planner_expansions());
		}
		m_population = g_world.creature_count();
		m_planned = g_world.planning_count();
	}

	void render(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		auto& debug = backend.debug();
		debug.clear();
		debug.filled_rect(20, 20, 620, 220, 0x00081018);
		debug.rect(20, 20, 620, 220, 0x00ffffff);
		debug.text(32, 32, "sim world - camara = LOD (izq/der), fuego = nueva criatura",
			   0x00ffffff);

		// La room de la camara, en planta, con las criaturas como puntos.
		debug.rect(32, 56, 608, 150, 0x00406080);
		for (eng::usize i = 0u; i < g_world.creature_count(); ++i) {
			const auto& c = g_world.creature(i);
			if (c.room != m_room || !c.alive()) {
				continue;
			}
			const eng::s16 px = static_cast<eng::s16>(32 + 12 + (c.x & 0x1ff) / 2);
			const eng::s16 py = static_cast<eng::s16>(56 + 12 + (c.y & 0x7f));
			debug.filled_rect(px, py, static_cast<eng::s16>(px + 3),
					  static_cast<eng::s16>(py + 3),
					  c.realized() ? 0x00ffd040 : 0x006080a0);
		}

		auto number = [](eng::u32 v) {
			static eng::util::StaticString<16> s;
			s.clear();
			(void)eng::util::to_chars_u32(s, v);
			return s.c_str();
		};
		char line[56];

		line[0] = '\0';
		append(line, "room: ");
		append(line, number(m_room));
		append(line, "   poblacion: ");
		append(line, number(m_population));
		debug.text(32, 162, line, 0x0080ff80);

		line[0] = '\0';
		append(line, "planes activos: ");
		append(line, number(m_planned));
		append(line, "   expansiones: ");
		append(line, number(m_expansions));
		debug.text(32, 186, line, 0x00ffff00);

		eng::debug::probe_when_ready(g_eng_run_status, 0u);
	}

private:
	EntityId spawn_one(eng::u8 seed) const noexcept {
		const RoomId room = static_cast<RoomId>(seed % kRooms);
		const EntityId id = g_world.spawn(static_cast<SpeciesId>(1u + (seed % 3u)), 0u, room,
						  static_cast<eng::s16>((seed * 37u) & 0x1ff),
						  static_cast<eng::s16>((seed * 53u) & 0x7f));
		return id;
	}

	eng::u8 m_room = 0u;
	eng::u16 m_population = 0u;
	eng::u16 m_planned = 0u;
	eng::u32 m_expansions = 0u;
	bool m_fire_held = false;
	bool m_memory_ok = false;
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::AmigaBackend backend {};
	static SimWorldGame game {};
	eng::Engine engine {backend, game};
	engine.run_frames_polling(0xffff);

	return 0;
}
