// ============================================================================
// Test HOST-102: primitivas de audio (eng::util::dsp).
// ============================================================================
//
// Respalda `eng/core/util/dsp.hpp`: Adsr, OnePole, DelayLine, soft_clip y osciladores.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/102_dsp

#include <cmath>
#include <cstdio>

#include <eng/core/minifloat_math.hpp>
#include <eng/core/util/dsp.hpp>
#include <eng/retro/fixed_q.hpp>

using eng::math::MiniFloat16;
namespace em = eng::math;
namespace eu = eng::util;
namespace er = eng::retro;

namespace {

int g_fail = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

} // namespace

int main() {
	std::printf("== HOST-102 dsp ==\n");

	// --- Adsr ----------------------------------------------------------------
	{
		eu::Adsr<double> env {0.4, 0.2, 0.25, 0.5};
		check(!env.active(), "ADSR inactiva");
		env.note_on();
		check(std::fabs(env.tick() - 0.4) <= 1.0e-12, "ADSR attack 1");
		check(std::fabs(env.tick() - 0.8) <= 1.0e-12, "ADSR attack 2");
		check(std::fabs(env.tick() - 1.0) <= 1.0e-12, "ADSR pico 1.0");
		check(std::fabs(env.tick() - 0.8) <= 1.0e-12, "ADSR decay 1");
		check(std::fabs(env.tick() - 0.6) <= 1.0e-12, "ADSR decay 2");
		check(std::fabs(env.tick() - 0.5) <= 1.0e-12, "ADSR sustain (clava en 0.5)");
		check(std::fabs(env.tick() - 0.5) <= 1.0e-12, "ADSR sostiene");
		env.note_off();
		check(std::fabs(env.tick() - 0.25) <= 1.0e-12, "ADSR release 1");
		check(std::fabs(env.tick() - 0.0) <= 1.0e-12, "ADSR release a 0");
		check(!env.active(), "ADSR termina");
	}

	// --- OnePole -------------------------------------------------------------
	{
		eu::OnePole<double> lp {0.5, 0.0};
		const double a = lp.process(1.0);
		const double b = lp.process(1.0);
		check(std::fabs(a - 0.5) <= 1.0e-12 && std::fabs(b - 0.75) <= 1.0e-12,
		      "OnePole converge");
		for (int i = 0; i < 40; ++i) {
			(void)lp.process(1.0);
		}
		check(std::fabs(lp.y - 1.0) <= 1.0e-3, "OnePole tiende a 1");
	}

	// --- DelayLine -----------------------------------------------------------
	{
		eu::DelayLine<double, 4> dl;
		dl.clear();
		dl.push(1.0);
		dl.push(2.0);
		dl.push(3.0);
		check(std::fabs(dl.read(0u) - 3.0) <= 1.0e-12, "delay read(0) = ultimo");
		check(std::fabs(dl.read(1u) - 2.0) <= 1.0e-12, "delay read(1)");
		check(std::fabs(dl.read(2u) - 1.0) <= 1.0e-12, "delay read(2)");
		check(std::fabs(dl.read(9u) - 0.0) <= 1.0e-12, "delay fuera de rango = 0");
		const double echo = dl.process(4.0, 1u);
		check(std::fabs(echo - 2.0) <= 1.0e-12, "delay process devuelve el eco");
		check(std::fabs(dl.read(0u) - 4.0) <= 1.0e-12, "delay guarda la entrada");
	}

	// --- soft_clip -----------------------------------------------------------
	{
		check(eu::soft_clip(0.5, 1.0) == 0.5, "soft_clip dentro");
		check(eu::soft_clip(2.0, 1.0) == 1.0, "soft_clip satura +");
		check(eu::soft_clip(-2.0, 1.0) == -1.0, "soft_clip satura -");
	}

	// --- osciladores ---------------------------------------------------------
	{
		check(std::fabs(eu::osc_saw(0.25) + 0.5) <= 1.0e-12, "saw(0.25) = -0.5");
		check(eu::osc_square(0.25) == 1.0 && eu::osc_square(0.75) == -1.0, "square");
		check(std::fabs(eu::osc_triangle(0.5) - 1.0) <= 1.0e-12, "triangle pico");
		check(std::fabs(eu::osc_triangle(0.0) + 1.0) <= 1.0e-12, "triangle inicio");
		check(std::fabs(eu::osc_sine(0.25) - 1.0) <= 1.0e-6, "sine(0.25) = 1");
		check(std::fabs(eu::osc_sine(0.0)) <= 1.0e-6, "sine(0) = 0");
	}

	// --- smoke MiniFloat16 ---------------------------------------------------
	{
		eu::OnePole<MiniFloat16> lp {MiniFloat16(0.5f), MiniFloat16(0.0f)};
		(void)lp.process(MiniFloat16(1.0f));
		check(em::to_double(lp.y) > 0.4 && em::to_double(lp.y) < 0.6, "MF OnePole ~0.5");
		eu::DelayLine<MiniFloat16, 4> dl;
		dl.clear();
		dl.push(MiniFloat16(0.75f));
		check(std::fabs(em::to_double(dl.read(0u)) - 0.75) <= 2.0e-3, "MF delay");
		const double saw = em::to_double(eu::osc_saw(MiniFloat16(0.25f)));
		check(std::fabs(saw + 0.5) <= 5.0e-3, "MF saw(0.25) ~ -0.5");
	}

	// --- fixed q12 (salvo osc_sine: necesita sin) ---------------------------
	{
		const auto q = [](float v) {
			return er::q12 {static_cast<eng::s16>(v * 4096.0f + 0.5f)};
		};
		eu::Adsr<er::q12> env {q(0.25f), q(0.25f), q(0.25f), q(0.5f)};
		env.note_on();
		check(std::fabs(em::to_double(env.tick()) - 0.25) <= 2.0e-3, "q12 ADSR attack");
		(void)env.tick(); // 0.5
		(void)env.tick(); // 0.75
		check(std::fabs(em::to_double(env.tick()) - 1.0) <= 2.0e-3, "q12 ADSR pico");
		for (int i = 0; i < 3; ++i) {
			(void)env.tick();
		}
		check(std::fabs(em::to_double(env.level) - 0.5) <= 2.0e-3, "q12 ADSR sustain");
		eu::OnePole<er::q12> lp {q(0.5f), q(0.0f)};
		(void)lp.process(q(1.0f));
		check(std::fabs(em::to_double(lp.y) - 0.5) <= 3.0e-3, "q12 OnePole ~0.5");
		eu::DelayLine<er::q12, 4> dl;
		dl.clear();
		dl.push(q(0.75f));
		check(std::fabs(em::to_double(dl.read(0u)) - 0.75) <= 2.0e-3, "q12 delay");
		check(std::fabs(em::to_double(eu::osc_saw(q(0.25f))) + 0.5) <= 2.0e-3, "q12 saw");
		check(er::q12 {0} < eu::osc_square(q(0.25f)) &&
			      er::q12 {0} < eu::osc_triangle(q(0.5f)),
		      "q12 square/triangle definidos");
	}

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: dsp validado.\n");
	return 0;
}
