// ============================================================================
// Test HOST-160: percepcion multimodal (vision, oido, olfato, tacto, gusto, temperatura)
// ============================================================================
//
// Valida `eng/sim/senses.hpp`:
//
//   1) `senses_from_species`: los sensores derivan de la especie.
//   2) Geometria: `sector_of`/`in_cone` (cono frontal) y `attenuation` (caida por distancia).
//   3) `perceive`: cada modalidad dispara segun alcance/cobertura; el cono esconde lo que
//      esta detras; el oido cruza regiones atenuado; lo invisible y silencioso no se
//      percibe; la novedad sube la saliencia (memoria de largo plazo).
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/160_sim_senses

#include <cstdio>

#include <eng/sim/senses.hpp>

namespace {

using namespace eng::sim;

unsigned g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

void test_geometry() {
	check(sector_of(1, 0) == 0u && sector_of(0, 1) == 2u && sector_of(-1, 0) == 4u,
	      "senses: sectores de direccion");
	check(sector_distance(0u, 1u) == 1u && sector_distance(0u, 4u) == 4u &&
		      sector_distance(7u, 1u) == 2u,
	      "senses: distancia circular entre sectores");

	Observer o {};
	o.face_x = 1;
	o.face_y = 0;
	check(in_cone(o, 5, 0, 100u), "senses: lo que esta delante entra en el cono");
	check(!in_cone(o, -5, 0, 100u), "senses: lo que esta detras queda fuera");

	Observer omni {};
	omni.face_x = 0;
	omni.face_y = 0;
	check(in_cone(omni, -5, 0, 100u), "senses: sin orientacion es 360");

	check(attenuation(0u, 20u) == 255u && attenuation(20u, 20u) == 0u &&
		      attenuation(10u, 20u) == 127u,
	      "senses: atenuacion lineal por distancia");
}

void test_from_species() {
	Species s {};
	s.vision = 80u;
	s.hearing = 60u;
	s.size = 40u;
	const Senses sn = senses_from_species(s);
	check(sn.vision == 80u && sn.hearing == 60u && sn.smell == 60u && sn.acuity == 70u,
	      "senses: los sensores derivan de la especie");
}

void test_perceive() {
	Senses s {};
	s.vision = 80u;
	s.hearing = 80u;
	s.smell = 60u;
	s.vision_range = 20u;
	s.hearing_range = 25u;
	s.smell_range = 12u;
	s.vision_arc = 100u;

	Observer o {};
	o.room = 0u;
	o.x = 0;
	o.y = 0;
	o.face_x = 1;
	o.face_y = 0;

	SenseTarget targets[5];
	targets[0] = SenseTarget {10u, 0u, 5, 0, 50u, 0u, 0u, 0u, 0u, true, TrackerKind::Prey};
	targets[1] = SenseTarget {11u, 0u, -5, 0, 50u, 200u, 0u, 0u, 0u, true, TrackerKind::Threat};
	targets[2] = SenseTarget {12u, 0u, 100, 0, 50u, 0u, 0u, 0u, 0u, true, TrackerKind::Prey};
	targets[3] = SenseTarget {13u, 0u, 3, 0, 50u, 0u, 0u, 0u, 0u, false, TrackerKind::Prey};
	targets[4] = SenseTarget {14u, 1u, 5, 0, 50u, 200u, 0u, 0u, 0u, true, TrackerKind::Friend};

	Observation obs[8];
	const eng::u8 n = perceive(s, o, eng::Span<const SenseTarget> {targets, 5},
				   eng::Span<Observation> {obs, 8}, [](EntityId) { return false; });
	check(n == 3u, "senses: solo se percibe lo detectable (3 de 5)");

	const Observation* a = nullptr;
	const Observation* b = nullptr;
	const Observation* e = nullptr;
	for (eng::u8 i = 0; i < n; ++i) {
		if (obs[i].target == 10u) a = &obs[i];
		if (obs[i].target == 11u) b = &obs[i];
		if (obs[i].target == 14u) e = &obs[i];
	}
	check(a != nullptr && (a->modalities & sense_bit::sight) != 0u,
	      "senses: lo visible y delante se ve");
	check(a != nullptr && a->novelty > 0u && a->salience > a->strength,
	      "senses: lo desconocido gana saliencia");
	check(b != nullptr && (b->modalities & sense_bit::sight) == 0u &&
		      (b->modalities & sense_bit::hearing) != 0u,
	      "senses: lo que esta detras se oye pero no se ve");
	check(e != nullptr && (e->modalities & sense_bit::hearing) != 0u,
	      "senses: el oido cruza la region contigua");
	check(e != nullptr && b != nullptr && e->strength < b->strength,
	      "senses: entre regiones se oye mas debil");

	// La novedad desaparece si ya se conoce al objetivo.
	const eng::u8 n2 = perceive(s, o, eng::Span<const SenseTarget> {targets, 5},
				    eng::Span<Observation> {obs, 8},
				    [](EntityId id) { return id == 10u; });
	check(n2 == 3u && obs[0].target == 10u && obs[0].novelty == 0u,
	      "senses: lo conocido no es novedad");
}

} // namespace

int main() {
	std::printf("Sim senses:\n");
	test_geometry();
	test_from_species();
	test_perceive();

	if (g_fail == 0u) {
		std::printf("OK: Sim senses (geometria, sentidos, novedad)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
