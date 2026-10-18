// ============================================================================
// Test HOST-476: controlador de presentacion (eng::graphics::PresentController)
// ============================================================================
//
// Valida la pieza de F2 que junta politica de paneo, pacing medido y publicacion
// diferida al inicio de campo:
//
//   1) Primer frame = Full (ancla) y la publicacion se difiere al frame siguiente
//      (`begin` reporta `publish` una vez por `commit`).
//   2) Offset de display = paneo avanzado respecto al Full publicado, clampeado al
//      margen de la politica.
//   3) Racha adaptativa: `observe_full` con un coste medido re-ajusta los fakes
//      seguidos (`affordable_fakes`).
//   4) Camino asincrono condicional al hardware: `async_ok` solo con Fast RAM.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/graphics/476_present_controller

#include <eng/graphics/present_controller.hpp>
#include <eng/hw/info.hpp>

#include <cstdio>

namespace {

using eng::graphics::PresentController;
using eng::graphics::PresentControllerConfig;
using eng::graphics::PresentStep;
using eng::hw::HwInfo;

unsigned g_fail = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

HwInfo hw_with(eng::u32 fast_bytes) {
	HwInfo h {};
	h.fast_ram_bytes = fast_bytes;
	return h;
}

void test_publish_and_offsets() {
	PresentController pc;
	pc.configure(PresentControllerConfig {eng::graphics::PanWindowPolicy {15, 0, 4}, 3u},
		     hw_with(0u));
	pc.reset(0);

	// Frame 0: primer paso = Full (ancla); nada pendiente que publicar; offset 0.
	PresentStep s = pc.begin(0);
	check(s.full && !s.publish && s.offset_x == 0, "frame 0: Full de ancla, sin publicar");
	pc.commit(0);
	check(pc.pending(), "tras commit hay Full pendiente");

	// Frame 1: publica el Full de pan 0; el offset del display = 1 (paneo avanzado).
	s = pc.begin(1);
	check(s.publish && s.offset_x == 1, "frame 1: publica el Full y muestra off 1");
	check(!pc.pending(), "la publicacion consume el pendiente");

	// Frame 2 sin commit nuevo: no hay publicacion; el offset crece sobre el mismo Full.
	s = pc.begin(2);
	check(!s.publish && s.offset_x == 2, "frame 2: offset 2 sobre el Full publicado");

	// Clamp al margen (15) con un paneo muy avanzado.
	s = pc.begin(40);
	check(s.offset_x == 15, "offset clampeado al margen de la politica");

	// Negativo: clamp por abajo.
	pc.reset(0);
	(void)pc.begin(0);
	pc.commit(0);
	s = pc.begin(-40);
	check(s.offset_x == -15, "offset clampeado al margen negativo");
}

void test_adaptive_run() {
	PresentController pc;
	pc.configure(PresentControllerConfig {eng::graphics::PanWindowPolicy {15, 0, 4}, 3u},
		     hw_with(0u));
	pc.reset(0);
	// Coste medido de la pasada: ~2 campos PAL (283k ciclos) -> affordable_fakes(3) = 0
	// (la base pide no interpolar: round(2)-2 = 0).
	pc.observe_full(283752u);
	PresentStep s = pc.begin(0);
	check(s.full, "coste 2 campos: primer frame Full");
	pc.commit(0);
	pc.observe_full(283752u);
	int fulls = 1;
	int fakes = 0;
	int publishes = 1; // el de commit(0) contara en el siguiente begin
	bool publishSeen = false;
	// 12 frames a 1 px: con K=0, Full cada frame.
	for (int i = 1; i < 12; ++i) {
		s = pc.begin(i);
		check(!publishSeen ? s.publish : true, "la publicacion llega en el primer begin");
		if (s.publish) {
			publishSeen = true;
			++publishes;
		}
		if (s.full) {
			++fulls;
			pc.commit(i);
		} else {
			++fakes;
		}
		pc.observe_full(283752u);
	}
	check(fulls + fakes == 12, "12 frames contabilizados");
	check(fulls >= 10, "K=0: Full casi cada frame");
	(void)publishes;
}

void test_async_gate() {
	PresentController pc;
	// Sin Fast RAM: sincrono.
	pc.configure(PresentControllerConfig {eng::graphics::PanWindowPolicy {15, 0, 1}, 3u},
		     hw_with(0u));
	check(!pc.async_ok(), "sin Fast RAM, async_ok = false");
	PresentStep s = pc.begin(0);
	check(!s.async_ok, "el paso refleja async_ok");
	// Con Fast RAM (p. ej. expansion de 1 MB): asincrono aconsejable.
	pc.configure(PresentControllerConfig {eng::graphics::PanWindowPolicy {15, 0, 1}, 3u},
		     hw_with(1024u * 1024u));
	check(pc.async_ok(), "con Fast RAM, async_ok = true");
	s = pc.begin(0);
	check(s.async_ok, "el paso refleja async_ok con Fast RAM");
}

} // namespace

int main() {
	test_publish_and_offsets();
	test_adaptive_run();
	test_async_gate();
	if (g_fail == 0) {
		std::printf("OK: present controller (publicacion diferida, offsets, racha y gate async)\n");
	} else {
		std::printf("FALLOS: %u\n", g_fail);
	}
	return g_fail == 0 ? 0 : 1;
}
