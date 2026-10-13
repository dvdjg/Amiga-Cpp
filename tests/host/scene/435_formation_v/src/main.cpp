// ============================================================================
// Test HOST-435: generadores de ola en V (`gen_formation_v` / `gen_formation_v_sine`).
// ============================================================================
//
// Respalda los generadores de formación F2.1 de `eng/scene/formation.hpp`
// (`ROADMAP_SPRITES_COMPLETO.md` §3.1):
//   - `gen_formation_v`: V clásica de shmup — miembros simétricos respecto al centro,
//     delay creciente hacia las puntas (la punta entra primero), apertura en Y de
//     `spread_x / 2` por paso y una trayectoria común.
//   - `gen_formation_v_sine`: misma geometría con **seno en espejo**: la mitad izquierda
//     (y el centro) usan `trajectory_left` y la derecha `trajectory_right`.
//
// La cabecera es genérica sobre el escalar (patrón del repo): se ejercita con `s32` y
// `float` (AGENTS §1.11), además del uso retro `s16`.
//
// Comprobaciones:
//   1) Geometría: offsets X simétricos (`x[i] == -x[n-1-i]`), Y por distancia al centro.
//   2) Delays: mínimo en la punta (0), creciente hacia las puntas por `delay_step`.
//   3) `open_down` invierte la apertura en Y; la trayectoria se copia a todos.
//   4) `gen_formation_v_sine`: reparto izquierda/centro vs derecha de las trayectorias.
//   5) Bordes: `count == 0`, `count == 1`, buffer más pequeño que `count` (tope `out.size()`).
//
// Ejecución:
//   bash tools/run-host-tests.sh tests/host/scene/435_formation_v   (solo este)
//   bash tools/run-host-tests.sh                                   (todos)

#include <cstdio>

#include <eng/scene/formation.hpp>

namespace {

using eng::scene::FormationMember;

int g_failures = 0;

#define CHECK(cond)                                                       \
	do {                                                                  \
		if (!(cond)) {                                                     \
			std::printf("  [FAIL] %s (linea %d)\n", #cond, __LINE__);      \
			++g_failures;                                                  \
		}                                                                 \
	} while (0)

using eng::scene::gen_formation_v;
using eng::scene::gen_formation_v_sine;

/// Misma formula con dos escalares: la cabecera no fija el tipo de coordenada.
template <typename S>
void test_v_generic() {
	FormationMember<S> out_buf[7] {};
	eng::Span<FormationMember<S>> out {out_buf, 7u};

	const eng::u16 n = gen_formation_v(out, 5u, static_cast<S>(28), 6u, 2u, false);
	CHECK(n == 5u);

	// X simétrica alrededor del centro; separacion `spread_x`.
	CHECK(out[0].offset.x() == static_cast<S>(-56));
	CHECK(out[1].offset.x() == static_cast<S>(-28));
	CHECK(out[2].offset.x() == static_cast<S>(0));
	CHECK(out[3].offset.x() == static_cast<S>(28));
	CHECK(out[4].offset.x() == static_cast<S>(56));
	// Y por distancia al centro (`dist * spread_x / 2`): punta plana arriba.
	CHECK(out[0].offset.y() == static_cast<S>(28));
	CHECK(out[1].offset.y() == static_cast<S>(14));
	CHECK(out[2].offset.y() == static_cast<S>(0));
	CHECK(out[3].offset.y() == static_cast<S>(14));
	CHECK(out[4].offset.y() == static_cast<S>(28));
	// Delays: la punta primero, brazos despues.
	CHECK(out[0].delay_ticks == 12u && out[1].delay_ticks == 6u);
	CHECK(out[2].delay_ticks == 0u);
	CHECK(out[3].delay_ticks == 6u && out[4].delay_ticks == 12u);
	// Trayectoria comun.
	for (eng::u8 i = 0u; i < 5u; ++i) {
		CHECK(out[i].trajectory == 2u);
	}
	// Lo que sobra del buffer no se toca.
	CHECK(out[5].delay_ticks == 0u && out[5].offset.x() == static_cast<S>(0));

	// `open_down` invierte la apertura en Y (sin tocar X ni delays).
	(void)gen_formation_v(out, 5u, static_cast<S>(28), 6u, 2u, true);
	CHECK(out[0].offset.y() == static_cast<S>(-28));
	CHECK(out[2].offset.y() == static_cast<S>(0));
	CHECK(out[4].offset.y() == static_cast<S>(-28));
	CHECK(out[0].offset.x() == static_cast<S>(-56));
}

void test_v_edges() {
	std::printf("gen_formation_v: bordes (0, 1 y buffer corto)\n");

	FormationMember<eng::s16> out[4] {};
	eng::Span<FormationMember<eng::s16>> span {out, 4u};

	CHECK(gen_formation_v(span, 0u, 28, 6u, 0u) == 0u);

	CHECK(gen_formation_v(span, 1u, 28, 6u, 7u) == 1u);
	CHECK(out[0].offset.x() == 0 && out[0].offset.y() == 0);
	CHECK(out[0].delay_ticks == 0u && out[0].trajectory == 7u);

	// Buffer mas pequeno que `count`: escribe los que caben (tope por `out.size()`).
	CHECK(gen_formation_v(span, 9u, 28, 6u, 1u) == 4u);
}

void test_v_sine() {
	std::printf("gen_formation_v_sine: brazos con trayectorias en espejo\n");

	FormationMember<eng::s16> out[6] {};
	eng::Span<FormationMember<eng::s16>> span {out, 6u};

	const eng::u16 n = gen_formation_v_sine(span, 6u, 24, 4u, 0u, 1u);
	CHECK(n == 6u);

	// half = 3: i<=3 -> izquierda (0); i>3 -> derecha (1). Centro incluido en izquierda.
	CHECK(out[0].trajectory == 0u && out[1].trajectory == 0u);
	CHECK(out[2].trajectory == 0u && out[3].trajectory == 0u);
	CHECK(out[4].trajectory == 1u && out[5].trajectory == 1u);

	// Geometria V con spread 24: X = 24*(i-3); Y = 12*dist (por distancia al centro).
	CHECK(out[0].offset.x() == -72 && out[0].offset.y() == 36);
	CHECK(out[3].offset.x() == 0 && out[3].offset.y() == 0);
	CHECK(out[5].offset.x() == 48 && out[5].offset.y() == 24);

	// Delays simetricos (punta 0; los brazos, por distancia).
	CHECK(out[0].delay_ticks == 12u && out[3].delay_ticks == 0u &&
	      out[5].delay_ticks == 8u);

	// count = 1: centro unico, trayectoria izquierda.
	CHECK(gen_formation_v_sine(span, 1u, 24, 4u, 3u, 4u) == 1u);
	CHECK(out[0].trajectory == 3u && out[0].offset.x() == 0);
}

} // namespace

int main() {
	std::printf("Test HOST-435 formation_v (generadores de ola en V, F2.1)\n");
	std::printf("=========================================================\n");

	std::printf("gen_formation_v<eng::s32>: geometria, delays y open_down\n");
	test_v_generic<eng::s32>();
	std::printf("gen_formation_v<float>: misma formula con otro escalar\n");
	test_v_generic<float>();
	test_v_edges();
	test_v_sine();

	if (g_failures == 0) {
		std::printf("OK: generadores de ola en V validados (s32/float/s16).\n");
		return 0;
	}
	std::printf("FAIL: %d comprobacion(es) fallaron\n", g_failures);
	return 1;
}
