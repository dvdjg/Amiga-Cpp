// ============================================================================
// Test HOST-431: secuenciador de valores y eventos (`eng/core/util/sequence.hpp`) - F8.
// ============================================================================
//
// Respalda `KeyTrack` (interpolación por claves con easings, genérica sobre el escalar:
// float y Fixed<s16,12>), `EventTrack` (orden y rebose), `Sequence` y `SequenceRunner`
// (ventanas de eventos por avance, cruce del final, loop y seek).
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/core/431_sequence

#include <cmath>
#include <cstdio>

#include <eng/core/math/fixed.hpp>
#include <eng/core/math/fixed_math.hpp>
#include <eng/core/util/sequence.hpp>

namespace {

int g_fail = 0;
void check(bool ok, const char* m) {
	if (!ok) {
		std::printf("[FAIL] %s\n", m);
		++g_fail;
	}
}

using eng::util::Ease;
using eng::util::EventTrack;
using eng::util::KeyTrack;
using eng::util::Sequence;
using eng::util::SequenceRunner;

/// Interpolación con `float`: lineal (extremos y medio), `Step`, `EaseIn` y retención.
void test_key_track_float() {
	KeyTrack<float, 4> t {};
	check(t.add(0u, 0.0f), "clave 0");
	check(t.add(10u, 4.0f, Ease::Linear), "clave 10");
	check(std::fabs(t.sample(0u) - 0.0f) < 1e-5f, "muestra 0");
	check(std::fabs(t.sample(5u) - 2.0f) < 1e-5f, "lineal a mitad");
	check(std::fabs(t.sample(10u) - 4.0f) < 1e-5f, "muestra final");
	check(std::fabs(t.sample(100u) - 4.0f) < 1e-5f, "retencion tras la ultima clave");

	KeyTrack<float, 4> step {};
	(void)step.add(0u, 0.0f);
	(void)step.add(10u, 8.0f, Ease::Step);
	check(std::fabs(step.sample(9u) - 0.0f) < 1e-5f, "Step mantiene el valor anterior");
	check(std::fabs(step.sample(10u) - 8.0f) < 1e-5f, "Step cambia en su tick");

	KeyTrack<float, 4> ease {};
	(void)ease.add(0u, 0.0f);
	(void)ease.add(10u, 4.0f, Ease::EaseIn);
	const float mid = ease.sample(5u);
	check(mid > 0.0f && mid < 1.9f, "EaseIn arranca por debajo del lineal");

	// Orden y capacidad.
	check(!ease.add(5u, 1.0f), "clave fuera de orden rechazada");
	(void)ease.add(20u, 1.0f);
	(void)ease.add(30u, 1.0f);
	check(!ease.add(40u, 1.0f), "capacidad agotada");
}

/// El mismo algoritmo con un escalar con fracción (`Fixed<s16,12>`).
void test_key_track_fixed() {
	using Fix = eng::math::Fixed<eng::s16, 12>;
	auto fint = [](int i) { return eng::math::scalar_traits<Fix>::from_int(i); };
	KeyTrack<Fix, 2> t {};
	// Ticks pequeños: `from_int` de 4.12 solo representa ±8 (el span entra en rango).
	check(t.add(0u, fint(0)), "clave fixed 0");
	check(t.add(4u, fint(4)), "clave fixed 4");
	check(t.sample(0u).v == fint(0).v, "fixed muestra 0");
	check(t.sample(2u).v == fint(2).v, "fixed mitad exacta");
	check(t.sample(4u).v == fint(4).v, "fixed muestra final");
}

/// Eventos: orden por pista, ventanas por avance, fin y `seek`.
void test_sequence_runner() {
	using Seq = Sequence<float, 2u, 4u, 4u>;
	Seq seq {};
	seq.length = 10u;
	check(seq.events[0].add(2u, 100u), "evento 100");
	check(seq.events[0].add(5u, 101u), "evento 101");
	check(seq.events[1].add(3u, 200u), "evento 200");
	check(!seq.events[1].add(1u, 201u), "evento fuera de orden rechazado");
	(void)seq.values[0].add(0u, 0.0f);
	(void)seq.values[0].add(10u, 4.0f);

	SequenceRunner r {};
	int fired = 0;
	eng::u16 ids[8] = {};
	eng::u32 ticks[8] = {};
	auto ev = [&](eng::u16 id, eng::u32 t) {
		if (fired < 8) {
			ids[fired] = id;
			ticks[fired] = t;
		}
		++fired;
	};

	// Sin `play` no avanza.
	r.advance(seq, 4u, ev);
	check(fired == 0 && r.tick == 0u, "en pausa no avanza");

	r.play();
	r.advance(seq, 4u, ev); // ventana [0,4): 2->100 y 3->200 (orden por pista)
	check(fired == 2 && ids[0] == 100u && ticks[0] == 2u && ids[1] == 200u && ticks[1] == 3u,
	      "eventos de la primera ventana");
	check(r.tick == 4u && !r.finished, "cursor 4");

	r.advance(seq, 3u, ev); // [4,7): 5->101
	check(fired == 3 && ids[2] == 101u, "evento de la segunda ventana");
	check(std::fabs(seq.values[0].sample(r.tick) - 2.8f) < 1e-5f, "valor en tick 7");

	r.advance(seq, 10u, ev); // cruza el final: [7,10) sin eventos -> finished
	check(fired == 3 && r.finished && r.tick == 10u, "fin sin loop");

	// seek: coloca el cursor sin disparar y marca el fin.
	SequenceRunner r2 {};
	r2.play();
	r2.seek(seq, 7u);
	check(r2.tick == 7u && !r2.finished, "seek dentro");
	r2.seek(seq, 10u);
	check(r2.finished, "seek al final -> finished");
}

/// Loop: una vuelta cerrada por avance, con el evento del final de la vuelta repetido.
void test_sequence_loop() {
	using Seq = Sequence<float, 1u, 2u, 2u>;
	Seq seq {};
	seq.length = 5u;
	seq.loop = true;
	(void)seq.events[0].add(3u, 7u);

	SequenceRunner r {};
	int fired = 0;
	auto ev = [&](eng::u16, eng::u32) { ++fired; };
	r.reset(true);
	r.advance(seq, 6u, ev); // [0,5) dispara el 3; envuelve y queda en 1
	check(fired == 1 && r.tick == 1u && !r.finished, "loop: primera vuelta");
	r.advance(seq, 4u, ev); // [1,5) dispara el 3 otra vez; vuelve a 0
	check(fired == 2 && r.tick == 0u && !r.finished, "loop: segunda vuelta");
}

} // namespace

int main() {
	std::printf("== HOST-431 sequence ==\n");

	test_key_track_float();
	test_key_track_fixed();
	test_sequence_runner();
	test_sequence_loop();

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: secuenciador de valores y eventos validado.\n");
	return 0;
}
