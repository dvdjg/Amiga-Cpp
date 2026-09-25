// ============================================================================
// Demo 001 (features/sim/amiga): benchmark del ecosistema (`eng::sim`) en Amiga.
// ============================================================================
//
// Mide el coste del **ecosistema** (un `SimWorld` con criaturas) y de su **planificacion**
// (GOAP con presupuesto; ademas se descompone una tarea HTN) sobre Amiga real/emulado, con
// el contador TOD de la CIA-A (50 Hz PAL), sin depender de que quepa en un frame.
//
// Mide **tres ventanas por separado** (para saber donde optimizar): tick del ecosistema solo,
// planificacion sola (un "pase" = todas las criaturas) y ambas juntas (la referencia).
//
// Publica en `g_eng_run_status.detail`:
//   bits 31..16  ticks de simulacion por segundo (sin planificar)
//   bits 15..0   frames/s con tick + planificacion (referencia)
//
// y lo dibuja en pantalla (ticks/s, pases/s, frames/s, expansiones/frame). Cambiar el target
// (`TARGET_MACHINE`) compararia CPU.
//
// El mundo (~24 KB) va en **memoria estatica**: en la pila del 68000 no cabe.
//
// Build/run/analyze (mismos wrappers que una demo):
//   bash tools/build/build-demo.sh demos/features/sim/amiga/001_sim_bench --release --clean
//   bash tools/run/run-demo.sh demos/features/sim/amiga/001_sim_bench --warp
//
// Verificacion: build -> run -> READY OK (evidencia en el README).

#include <eng/api/api.hpp>
#include <eng/core/math/random.hpp>
#include <eng/core/util/intmath.hpp>
#include <eng/core/util/static_string.hpp>
#include <eng/core/util/text.hpp>
#include <eng/platform/amiga/backend.hpp>

#include <eng/ai/planning/htn.hpp>
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

using World = SimWorld<SimTraits, 12, 4, 4, 8>;
using Htn = HtnDriver<32u, 5u, 1u, 4u, 8u>;

constexpr eng::u8 kCreatures = 12u;
constexpr eng::u16 kPlanBudget = 24u;  // presupuesto de expansiones por busqueda
constexpr eng::u32 kPhaseTicks = 100u; // 2 s emulados (50 Hz) por ventana de medida
constexpr eng::u32 kMaxIter = 20000u;

// Memoria **estatica** (no pila): el mundo (~24 KB), la red HTN y el generador aleatorio.
World g_world {};
Htn g_htn {};
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

/// Tick del ecosistema, **sin** planificar.
void tick_world() {
	g_world.tick_realized(g_rng);
	g_world.tick_abstract(g_rng);
}

/// Un **pase de planificacion**: todas las criaturas planifican una vez. Devuelve las
/// expansiones de GOAP acumuladas.
eng::u32 plan_pass(eng::u32 frame, eng::Span<const SimGoap::Action> acts,
		   const SimGoap::Goal& goal, const PlanParams& params) {
	eng::u32 expansions = 0u;
	const SimGoap::State start = start_state(SimInventory {});
	for (eng::usize i = 0u; i < g_world.creature_count(); ++i) {
		const EntityId id = g_world.creature(i).id;
		(void)g_world.plan_tick(id, start, goal, acts, static_cast<eng::u16>(frame),
					params);
		expansions += static_cast<eng::u32>(g_world.planner_expansions());
	}
	return expansions;
}

struct SimBench {
	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		m_memory_ok = backend.configure_memory({4096, 4096, 1024});

		// Poblacion + grafo de salas.
		g_world.link_rooms(0u, 1u);
		g_world.link_rooms(1u, 2u);
		for (eng::u8 i = 0u; i < kCreatures; ++i) {
			(void)g_world.spawn(static_cast<SpeciesId>(1u + (i % 3u)), 0u,
					    static_cast<RoomId>(i % 3u),
					    static_cast<eng::s16>(i * 8), 0);
		}
		m_creatures = g_world.creature_count();
		// Todas curiosas y autonomas: asi la histeresis deja que planifiquen.
		for (eng::usize i = 0u; i < g_world.creature_count(); ++i) {
			g_world.creature(i).personality.curiosity = 200u;
			g_world.creature(i).personality.autonomy = 200u;
		}

		// Red HTN del dominio de construccion (se descompone una vez para medirla).
		g_htn.set_domain(build_shelter_htn());
		m_htn_steps = g_htn.replan(start_state(SimInventory {}),
					   g_htn.domain().compound_at(0u),
					   ConstructionDomain::actions().span())
				  ? g_htn.runner().count
				  : 0u;

		const auto acts = ConstructionDomain::actions();
		const SimGoap::Goal goal = ConstructionDomain::goal(false, true);

		// Ventana 1: **tick solo** (sin planificar).
		const eng::u32 t_tick = backend.cia_tod_ticks();
		eng::u32 ticks = 0u;
		eng::u32 elapsed = 0u;
		while (ticks < kMaxIter) {
			tick_world();
			++ticks;
			elapsed = (backend.cia_tod_ticks() - t_tick) & 0x00ffffffu;
			if (elapsed >= kPhaseTicks) {
				break;
			}
		}
		m_tick_per_s = elapsed == 0u ? 0u : eng::util::div32(ticks * 50u, elapsed);

		// Ventana 2: **busqueda sola** (todas las criaturas, sin cache: se vacia cada pase).
		PlanParams stress {};
		stress.budget = kPlanBudget;
		stress.replan_interval = 0u; // todas, cada pase
		const eng::u32 t_plan = backend.cia_tod_ticks();
		eng::u32 passes = 0u;
		eng::u32 expansions = 0u;
		elapsed = 0u;
		while (passes < kMaxIter) {
			g_world.clear_plan_cache();
			expansions += plan_pass(passes, acts.span(), goal, stress);
			++passes;
			elapsed = (backend.cia_tod_ticks() - t_plan) & 0x00ffffffu;
			if (elapsed >= kPhaseTicks) {
				break;
			}
		}
		m_plan_pass_per_s = elapsed == 0u ? 0u : eng::util::div32(passes * 50u, elapsed);
		const eng::u32 per_pass = passes == 0u ? 0u : expansions / passes;
		m_exp_per_frame = per_pass > 0xffffu ? 0xffffu : per_pass;

		// Ventana 3: **tick + planificacion realista** (con cache e intervalo de replan).
		PlanParams reali {};
		reali.budget = kPlanBudget;
		reali.replan_interval = 120u; // uso normal: se replanifica de vez en cuando
		const eng::u32 t_both = backend.cia_tod_ticks();
		eng::u32 frames = 0u;
		elapsed = 0u;
		while (frames < kMaxIter) {
			tick_world();
			(void)plan_pass(frames, acts.span(), goal, reali);
			++frames;
			elapsed = (backend.cia_tod_ticks() - t_both) & 0x00ffffffu;
			if (elapsed >= kPhaseTicks) {
				break;
			}
		}
		m_combined_per_s = elapsed == 0u ? 0u : eng::util::div32(frames * 50u, elapsed);
		m_frames = frames;
		m_expansions = expansions;

		const eng::u32 tick_clamped = m_tick_per_s > 0xffffu ? 0xffffu : m_tick_per_s;
		const eng::u32 both_clamped =
		    m_combined_per_s > 0xffffu ? 0xffffu : m_combined_per_s;
		g_eng_run_status.detail = (tick_clamped << 16u) | both_clamped;
		eng::debug::mark_ready(g_eng_run_status, g_eng_run_status.detail);
	}

	void update(eng::amiga::AmigaBackend&, eng::GameContext& context) {
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
	}

	void render(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		auto& debug = backend.debug();
		debug.clear();
		debug.filled_rect(40, 40, 560, 280, 0x00081018);
		debug.rect(40, 40, 560, 280, 0x00ffffff);
		debug.text(60, 60, "sim bench - eng::sim on Amiga", 0x00ffffff);

		auto number = [](eng::u32 v) {
			static eng::util::StaticString<16> s;
			s.clear();
			(void)eng::util::to_chars_u32(s, v);
			return s.c_str();
		};
		char line[56];

		line[0] = '\0';
		append(line, "criaturas: ");
		append(line, number(m_creatures));
		debug.text(60, 90, line, 0x0080ff80);

		line[0] = '\0';
		append(line, "tick solo/s: ");
		append(line, number(m_tick_per_s));
		debug.text(60, 120, line, 0x00ffff00);

		line[0] = '\0';
		append(line, "pases plan/s: ");
		append(line, number(m_plan_pass_per_s));
		debug.text(60, 150, line, 0x00ff9000);

		line[0] = '\0';
		append(line, "frames/s (tick+plan): ");
		append(line, number(m_combined_per_s));
		debug.text(60, 180, line, 0x00ffff80);

		line[0] = '\0';
		append(line, "expansiones GOAP/frame: ");
		append(line, number(m_exp_per_frame));
		debug.text(60, 210, line, 0x00ffc040);

		line[0] = '\0';
		append(line, "pasos HTN (refugio): ");
		append(line, number(m_htn_steps));
		debug.text(60, 240, line, 0x00c080ff);

		eng::debug::probe_when_ready(g_eng_run_status, 0u);
	}

private:
	eng::u32 m_frames = 0u;
	eng::u32 m_expansions = 0u;
	eng::u32 m_tick_per_s = 0u;
	eng::u32 m_plan_pass_per_s = 0u;
	eng::u32 m_combined_per_s = 0u;
	eng::u32 m_exp_per_frame = 0u;
	eng::u16 m_htn_steps = 0u;
	eng::u16 m_creatures = 0u;
	bool m_memory_ok = false;
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::AmigaBackend backend {};
	static SimBench game {};
	eng::Engine engine {backend, game};
	engine.run_frames_polling(0xffff);

	return 0;
}
