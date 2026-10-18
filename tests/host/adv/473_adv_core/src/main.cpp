// ============================================================================
// Test HOST-473: nucleo de contenido de aventura -- walkboxes, hotspots y sala
// ============================================================================
//
// Valida `engine/include/eng/adv/{types,walkbox,hotspot,room}.hpp`, la capa de CONTENIDO de
// la aventura (destino del SIM v2 del pipeline de importacion; docs/engine/architecture/
// ADVENTURE_LAYER.md, ROADMAP_ADVENTURE_IMPORT.md):
//
//   1) `Walkbox`: `valid()` descarta el centinela SCUMM (-32000), `contains()` acepta los dos
//      sentidos de giro (borde incluido) y `closest_point()` da el punto de borde mas cercano
//      con su distancia al cuadrado (walk-to fuera de caja).
//   2) `box_at()`: gana la ULTIMA caja que contiene el punto (frente), las cajas centinela se
//      saltan y fuera de todo devuelve `kWalkboxInvalid`.
//   3) `Hotspot`/`hit_test()`: rectangulo con bordes incluidos, gana el ultimo (frente) y
//      sin pareja devuelve `kInvalidHotspot`.
//   4) `RoomContent::ok()`: exige id valido y dimensiones positivas; los campos son vistas
//      (`Span`), no copias.
//   5) Generalidad de escalar (§1.11): toda la geometria se ejercita con `eng::s16` y `float`.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/adv/473_adv_core

#include <cstdio>

#include <eng/adv/hotspot.hpp>
#include <eng/adv/room.hpp>
#include <eng/adv/types.hpp>
#include <eng/adv/walkbox.hpp>

namespace {

unsigned g_fail = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

using namespace eng::adv;

// Sala de pruebas compartida por ambos escalares: mundo 320x200, dos cajas solapadas
// (la segunda es el frente) y una caja centinela detras del todo.
template <class S>
void run_geometry_checks(const char* label) {
	using V = eng::math::Vec<2, S>;
	const S c0 = 0, c16 = 16, c32 = 32, c48 = 48, c64 = 64, c96 = 96, c320 = 320, c200 = 200;

	// Caja 0: 0..64 x 0..64 (antihorario en pantalla: ul,ur,lr,ll).
	Walkbox<S> box0 {{{c0, c0}}, {{c64, c0}}, {{c64, c64}}, {{c0, c64}}, 0x0f, 0x00, 255};
	// Caja 1: 32..96 x 32..96 (se solapa con la 0; es la del frente).
	Walkbox<S> box1 {{{c32, c32}}, {{c96, c32}}, {{c96, c96}}, {{c32, c96}}, 0x0f, 0x00, 128};
	// Caja 2: centinela SCUMM (-32000 en las cuatro esquinas) -> no utilizable.
	const S sent = static_cast<S>(-32000);
	Walkbox<S> box_bad {{{sent, sent}}, {{sent, sent}}, {{sent, sent}}, {{sent, sent}}, 0x00, 0x00, 255};

	check(box0.valid() && box1.valid(), "walkbox: cajas normales validas");
	check(!box_bad.valid(), "walkbox: centinela -32000 descartado");

	// contains: dentro, fuera y borde; tambien con giro horario (convenio invertido).
	check(box0.contains(V {{c32, c32}}), "walkbox: punto interior");
	check(!box0.contains(V {{c96, c32}}), "walkbox: punto exterior");
	check(box0.contains(V {{c64, c32}}), "walkbox: borde incluido");
	Walkbox<S> cw {{{c0, c0}}, {{c0, c64}}, {{c64, c64}}, {{c64, c0}}, 0x0f, 0x00, 255}; // horario
	check(cw.contains(V {{c16, c16}}) && !cw.contains(V {{c96, c16}}), "walkbox: giro invertido");

	// closest_point: desde (320,32) el borde mas cercano de la caja 0 es x=64.
	// La distancia se devuelve en el tipo PROMOVIDO (s16 -> int): 256^2 no cabe en s16.
	walkbox_dist_t<S> d2 {};
	const V q = box0.closest_point(V {{c320, c32}}, d2);
	check(q.v[0] == c64 && q.v[1] == c32, "walkbox: punto de borde mas cercano");
	check(d2 == static_cast<walkbox_dist_t<S>>(65536), "walkbox: distancia al cuadrado");
	// Desde esquina exterior: el punto mas cercano es la esquina.
	walkbox_dist_t<S> d3 {};
	const V q2 = box0.closest_point(V {{static_cast<S>(-10), static_cast<S>(-10)}}, d3);
	check(q2.v[0] == c0 && q2.v[1] == c0 && d3 == static_cast<walkbox_dist_t<S>>(200), "walkbox: esquina mas cercana");

	// box_at: gana la ultima caja (frente); el centinela inicial no cuenta.
	const Walkbox<S> boxes[3] {box_bad, box0, box1};
	const eng::Span<const Walkbox<S>> span {boxes};
	check(box_at(span, V {{c16, c16}}) == 1u, "box_at: primera caja cuando la ultima no contiene");
	check(box_at(span, V {{c48, static_cast<S>(48)}}) == 2u, "box_at: gana la caja del frente");
	check(box_at(span, V {{static_cast<S>(300), static_cast<S>(48)}}) == kWalkboxInvalid,
	      "box_at: fuera de todo -> invalido");

	// Hotspots: el ultimo (frente) gana; bordes incluidos; fuera -> invalido.
	const Hotspot<S> hs[2] {
		{{{c0, c0}}, {{c64, c64}}, 10u},
		{{{c32, c32}}, {{c96, c96}}, 20u},
	};
	const eng::Span<const Hotspot<S>> hspan {hs};
	check(hit_test(hspan, V {{c16, c16}}) == 10u, "hotspot: solo el primero");
	check(hit_test(hspan, V {{c48, c48}}) == 20u, "hotspot: gana el del frente");
	check(hit_test(hspan, V {{static_cast<S>(200), static_cast<S>(200)}}) == kInvalidHotspot,
	      "hotspot: fuera -> invalido");
	check(Hotspot<S> {{{c0, c0}}, {{c64, c64}}, 1u}.contains(V {{c64, static_cast<S>(64)}}),
	      "hotspot: borde incluido");
	// Rectangle con min/max en cualquier orden (el SIM no lo garantiza).
	check(Hotspot<S> {{{c64, c64}}, {{c0, c0}}, 1u}.contains(V {{c32, c32}}),
	      "hotspot: min/max invertidos");

	// RoomContent: vistas sobre las tablas + validez minima.
	const RoomContent<S> room {7u, static_cast<eng::u16>(c320), static_cast<eng::u16>(c200), span, hspan};
	check(room.ok(), "sala: contenido valido");
	check(!(RoomContent<S> {}).ok(), "sala: contenido por defecto no valido");
	check(room.walkboxes.size() == 3u && room.hotspots.size() == 2u, "sala: vistas sin copia");
	(void)label;
}

void run_all(const char* label) {
	run_geometry_checks<eng::s16>(label);
	run_geometry_checks<float>(label);
}

} // namespace

int main() {
	std::printf("Adv core (walkbox, hotspot, sala):\n");
	run_all("s16");
	run_all("float");
	if (g_fail != 0u) {
		std::printf("FAIL: %u comprobacion(es)\n", g_fail);
		return 1;
	}
	std::printf("OK: adv core (Walkbox/box_at/Hotspot/hit_test/RoomContent, s16+float)\n");
	return 0;
}
