// ============================================================================
// Test HOST-480: verbos, frase (AdvAction/resolve_action), prioridad de puntero y cola de
// réplicas (`eng::adv::VerbSet` / `pick_pointer` / `SayQueue`).
// ============================================================================
//
// Cubre el hito A1 de `ROADMAP_ADVENTURE.md` en su parte pura (sin mundo):
//   - `VerbSet`: alta hasta capacidad, ids válidos, búsqueda por semántica, cursor por verbo.
//   - `resolve_action`: tabla verbo×objetos — aridad `Target` (objetivo sí, sujeto no) y
//     `TwoObjects` (sujeto y objetivo), verbo inválido y contrato de «no tocar `out`».
//   - `pick_pointer`: prioridad GUI → hotspot (el más al frente) → suelo.
//   - `SayQueue`: FIFO, duración exacta por VBlank (la siguiente NO pierde el tick de la
//     retirada), rechazo con cola llena y de duración 0, `clear`.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/adv/480_adv_verbs_say

#include <eng/adv/say.hpp>
#include <eng/adv/verb.hpp>

#include <cstdio>

using namespace eng;
using namespace eng::adv;

namespace {

int g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

VerbSet<4> make_verbs() {
	VerbSet<4> s {};
	check(s.add("MIRA", VerbSemantic::Look, VerbArity::Target, CursorMode::Look) == 0u,
	      "VerbSet: primer verbo id 0");
	check(s.add("USA", VerbSemantic::Use, VerbArity::TwoObjects, CursorMode::Use) == 1u,
	      "VerbSet: segundo verbo id 1");
	check(s.add("IR", VerbSemantic::WalkTo, VerbArity::Target, CursorMode::Walk) == 2u,
	      "VerbSet: tercer verbo id 2");
	check(s.add("HABLA", VerbSemantic::Talk, VerbArity::Target, CursorMode::Talk) == 3u,
	      "VerbSet: cuarto verbo id 3");
	return s;
}

} // namespace

int main() {
	// 1) VerbSet: capacidad, consultas y desbordamiento.
	{
		VerbSet<4> s = make_verbs();
		check(s.add("X", VerbSemantic::Give, VerbArity::TwoObjects, CursorMode::Default) ==
			      kInvalidVerb,
		      "VerbSet: lleno -> kInvalidVerb");
		check(s.size() == 4u, "VerbSet: size 4");
		check(s.valid(3u) && !s.valid(4u) && !s.valid(kInvalidVerb), "VerbSet: valid");
		check(s.find(VerbSemantic::Use) == 1u, "VerbSet: find(Use) = 1");
		check(s.find(VerbSemantic::Open) == kInvalidVerb, "VerbSet: find sin verbo -> inválido");
		check(s.cursor_for(2u) == CursorMode::Walk, "VerbSet: cursor por verbo");
		check(s.cursor_for(9u) == CursorMode::Default, "VerbSet: cursor de id inválido -> Default");
		check(s.at(0u).arity == VerbArity::Target && s.at(1u).arity == VerbArity::TwoObjects,
		      "VerbSet: aridad de la tabla");
	}

	// 2) resolve_action: aridad Target (objetivo sí, sujeto no).
	{
		VerbSet<4> s = make_verbs();
		ResolvedAction out {};
		check(resolve_action(s, AdvAction {0u, kNoObject, 7u}, out), "resolve: Target ok");
		check(out.semantic == VerbSemantic::Look && out.subject == kNoObject && out.target == 7u,
		      "resolve: Target normaliza semántica y sujetos");
		check(!resolve_action(s, AdvAction {0u, 3u, 7u}, out), "resolve: Target con sujeto -> false");
		check(!resolve_action(s, AdvAction {0u, kNoObject, kNoObject}, out),
		      "resolve: Target sin objetivo -> false");
	}

	// 3) resolve_action: TwoObjects y contrato de «no tocar out» al fallar.
	{
		VerbSet<4> s = make_verbs();
		ResolvedAction out {};
		check(resolve_action(s, AdvAction {1u, 5u, 7u}, out), "resolve: TwoObjects ok");
		check(out.semantic == VerbSemantic::Use && out.subject == 5u && out.target == 7u,
		      "resolve: TwoObjects conserva sujetos");
		check(!resolve_action(s, AdvAction {1u, kNoObject, 7u}, out),
		      "resolve: TwoObjects sin sujeto -> false");
		check(!resolve_action(s, AdvAction {1u, 5u, kNoObject}, out),
		      "resolve: TwoObjects sin objetivo -> false");
		check(!resolve_action(s, AdvAction {kInvalidVerb, 5u, 7u}, out),
		      "resolve: verbo inválido -> false");
		// Contrato de fallo: `out` queda como estaba (centinela visible).
		ResolvedAction guard {VerbSemantic::Pull, 9u, 9u};
		check(!resolve_action(s, AdvAction {1u, kNoObject, 7u}, guard) &&
			      guard.semantic == VerbSemantic::Pull && guard.subject == 9u &&
			      guard.target == 9u,
		      "resolve: fallo no toca out");
	}

	// 4) pick_pointer: prioridad GUI -> hotspot (último = más al frente) -> suelo.
	{
		using S = s16;
		Hotspot<S> spots[2] {};
		spots[0].id = 10u;
		spots[0].min = eng::math::Vec<2, S> {0, 0};
		spots[0].max = eng::math::Vec<2, S> {100, 100};
		spots[1].id = 20u;
		spots[1].min = eng::math::Vec<2, S> {50, 50}; // solapa con el anterior: está más al frente
		spots[1].max = eng::math::Vec<2, S> {150, 150};
		const eng::Span<const Hotspot<S>> span {spots, 2u};

		const PointerHit gui = pick_pointer(true, span, eng::math::Vec<2, S> {60, 60});
		check(gui.kind == PointerKind::Gui, "pick: GUI gana al hotspot");
		const PointerHit depth = pick_pointer(false, span, eng::math::Vec<2, S> {60, 60});
		check(depth.kind == PointerKind::Hotspot && depth.id == 20u,
		      "pick: hotspot por profundidad (el último gana)");
		const PointerHit only = pick_pointer(false, span, eng::math::Vec<2, S> {10, 10});
		check(only.kind == PointerKind::Hotspot && only.id == 10u, "pick: hotspot único");
		const PointerHit floor = pick_pointer(false, span, eng::math::Vec<2, S> {200, 10});
		check(floor.kind == PointerKind::Floor && floor.id == kInvalidHotspot,
		      "pick: fuera de hotspot -> suelo");
	}

	// 5) SayQueue: FIFO y duración exacta por VBlank.
	{
		SayQueue<4> q {};
		check(q.empty() && q.current() == nullptr, "say: vacía al nacer");
		check(q.push(11u, 1u, 3u), "say: push 1");
		check(q.push(22u, 2u, 2u), "say: push 2");
		check(q.size() == 2u && q.current() != nullptr && q.current()->text_id == 11u,
		      "say: current = primera");
		q.tick(); // queda 2
		check(q.current()->frames == 2u, "say: tick descuenta");
		q.tick(); // queda 1
		q.tick(); // retira la 1.ª; la 2.ª pasa a current con su duración ÍNTEGRA
		check(q.size() == 1u && q.current()->text_id == 22u && q.current()->speaker == 2u &&
			      q.current()->frames == 2u,
		      "say: la siguiente no pierde el tick de la retirada");
		q.tick();
		check(q.size() == 1u && q.current()->text_id == 22u, "say: 2.ª sigue en curso");
		q.tick();
		check(q.empty() && q.current() == nullptr, "say: cola drenada");
	}

	// 6) SayQueue: límites y clear.
	{
		SayQueue<2> q {};
		check(q.push(1u, 0u, 5u) && q.push(2u, 0u, 5u), "say: llena a capacidad");
		check(!q.push(3u, 0u, 5u), "say: llena -> false");
		check(!q.push(4u, 0u, 0u), "say: duración 0 -> false");
		q.clear();
		check(q.empty() && q.size() == 0u, "say: clear");
		check(q.push(9u, 0u, 1u), "say: clear libera la cola");
		q.tick();
		check(q.empty(), "say: duración 1 dura exactamente 1 tick");
	}

	if (g_fail != 0) {
		std::printf("HOST-480 FALLO: %d comprobaciones\n", g_fail);
		return 1;
	}
	std::printf("HOST-480 OK\n");
	return 0;
}
