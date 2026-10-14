// ============================================================================
// Test HOST-438: cuerda/puente flexible y mecanismos (F6).
// ============================================================================
//
// Respalda `eng/scene/rope.hpp` (verlet + restricciones de distancia + contacto del
// actor) y `eng/scene/mechanisms.hpp` (muelle + catapulta con extremo por tabla de seno),
// genéricos sobre el escalar (AGENTS §1.11): se ejercitan con `s32` y `float` (y la
// catapulta también en `s16`).
//
// Comprobaciones:
//   1) Verlet determinista (mismos ticks -> mismas posiciones), extremos anclados.
//   2) Restricciones: la longitud entre puntos se mantiene ≈ `rest_length` bajo gravedad.
//   3) Impulso verlet: empuja el punto en la dirección pedida en el siguiente paso.
//   4) Contacto: proyección sobre el segmento, apoyo de los pies, impulso repartido y
//      rebote (0 = pegajoso, 256 = rebote total).
//   5) Muelle: compresión con el peso, impulso al soltar y retorno a la posición de reposo.
//   6) Catapulta: carga, disparo único, parada en `max_angle` y extremo exacto en los
//      ángulos notables (0 = derecha; 64 = arriba).
//
// Ejecución:
//   bash tools/run-host-tests.sh tests/host/scene/438_rope_mechanisms   (solo este)
//   bash tools/run-host-tests.sh                                       (todos)

#include <cstdio>

#include <eng/scene/mechanisms.hpp>
#include <eng/scene/rope.hpp>

namespace {

int g_failures = 0;

#define CHECK(cond)                                                       \
	do {                                                                  \
		if (!(cond)) {                                                     \
			std::printf("  [FAIL] %s (linea %d)\n", #cond, __LINE__);      \
			++g_failures;                                                  \
		}                                                                 \
	} while (0)

using eng::math::Vec;
using eng::scene::Rope;
using eng::scene::RopeContactOf;
using eng::scene::RopePoint;
using eng::scene::rope_apply_impulse;
using eng::scene::rope_find_contact;
using eng::scene::rope_resolve_player;
using eng::scene::rope_step;

template <typename S>
void make_chain(RopePoint<S>* pts, eng::usize n, S y, S step) {
	for (eng::usize i = 0u; i < n; ++i) {
		pts[i].p.v[0] = static_cast<S>(i * step);
		pts[i].p.v[1] = y;
		pts[i].prev = pts[i].p;
		pts[i].pinned = (i == 0u || i + 1u == n);
	}
}

template <typename S>
void test_rope() {
	constexpr eng::usize kPts = 6u;
	const S step = static_cast<S>(10);
	const S y0 = static_cast<S>(50);

	RopePoint<S> a[kPts] {};
	RopePoint<S> b[kPts] {};
	make_chain<S>(a, kPts, y0, step);
	make_chain<S>(b, kPts, y0, step);

	Rope<S> ra {eng::Span<RopePoint<S>> {a, kPts}, step, static_cast<S>(1), 2u, 256u};
	Rope<S> rb {eng::Span<RopePoint<S>> {b, kPts}, step, static_cast<S>(1), 2u, 256u};

	// 1) Determinismo + extremos anclados.
	for (eng::u8 t = 0u; t < 40u; ++t) {
		rope_step(ra);
		rope_step(rb);
	}
	bool same = true;
	for (eng::usize i = 0u; i < kPts; ++i) {
		if (a[i].p.v[0] != b[i].p.v[0] || a[i].p.v[1] != b[i].p.v[1]) same = false;
	}
	CHECK(same);
	CHECK(a[0].p.v[0] == static_cast<S>(0) && a[0].p.v[1] == y0);      // anclado izq
	CHECK(a[kPts - 1u].p.v[0] == static_cast<S>((kPts - 1u) * step)); // anclado der

	// 2) Restricciones: las distancias se mantienen cerca de `rest_length` (la gravedad
	// estira; con K=2 la desviación queda acotada).
	for (eng::usize i = 0u; i + 1u < kPts; ++i) {
		const S dx = static_cast<S>(a[i + 1u].p.v[0] - a[i].p.v[0]);
		const S dy = static_cast<S>(a[i + 1u].p.v[1] - a[i].p.v[1]);
		const S dist_sq = static_cast<S>(dx * dx + dy * dy);
		const S dist = eng::math::scalar_sqrt<S>::op(dist_sq);
		const S delta = dist > step ? static_cast<S>(dist - step) : static_cast<S>(step - dist);
		CHECK(delta <= static_cast<S>(3)); // cota de holgura tras 40 ticks
	}
	// La gravedad tiró de la cadena hacia abajo (no está recta).
	CHECK(a[kPts / 2u].p.v[1] > y0);

	// 3) Impulso verlet: un empujón hacia abajo acelera ese punto.
	RopePoint<S> c[kPts] {};
	make_chain<S>(c, kPts, y0, step);
	Rope<S> rc {eng::Span<RopePoint<S>> {c, kPts}, step, static_cast<S>(0), 2u, 256u};
	Vec<2, S> down {};
	down.v[1] = static_cast<S>(4);
	rope_apply_impulse(rc, 3u, down);
	rope_step(rc);
	CHECK(c[3u].p.v[1] > y0); // se movió hacia abajo respecto a la cadena recta

	// 4) Contacto + resolución.
	RopePoint<S> d[kPts] {};
	make_chain<S>(d, kPts, static_cast<S>(100), step);
	Rope<S> rd {eng::Span<RopePoint<S>> {d, kPts}, step, static_cast<S>(0), 2u, 256u};
	Vec<2, S> feet {};
	feet.v[0] = static_cast<S>(25); // sobre el punto medio (segmento 2: x 20..30)
	feet.v[1] = static_cast<S>(101); // 1 px por debajo de la cuerda (se apoya)
	const RopeContactOf<S> hit = rope_find_contact(rd, feet, static_cast<S>(6));
	CHECK(hit.hit && hit.segment == 2u);
	CHECK(hit.surface_y == static_cast<S>(100));

	Vec<2, S> far {};
	far.v[0] = static_cast<S>(25);
	far.v[1] = static_cast<S>(120);
	CHECK(!rope_find_contact(rd, far, static_cast<S>(6)).hit); // demasiado abajo

	S vy = static_cast<S>(8);
	rope_resolve_player(rd, feet, vy, hit, 0u);
	CHECK(feet.v[1] == static_cast<S>(100) && vy == static_cast<S>(0)); // pegajoso
	rope_step(rd);                                                      // el impulso se ve al integrar
	CHECK(d[2u].p.v[1] > static_cast<S>(100)); // el segmento fue empujado hacia abajo

	// Rebote total: vy = -vy.
	RopePoint<S> e[kPts] {};
	make_chain<S>(e, kPts, static_cast<S>(100), step);
	Rope<S> re {eng::Span<RopePoint<S>> {e, kPts}, step, static_cast<S>(0), 2u, 256u};
	Vec<2, S> feet2 {};
	feet2.v[0] = static_cast<S>(25);
	feet2.v[1] = static_cast<S>(101);
	const RopeContactOf<S> hit2 = rope_find_contact(re, feet2, static_cast<S>(6));
	S vy2 = static_cast<S>(8);
	rope_resolve_player(re, feet2, vy2, hit2, 256u);
	CHECK(vy2 == static_cast<S>(-8));
}

template <typename S>
void test_spring() {
	using eng::scene::Spring;
	using eng::scene::spring_update;

	Spring<S> sp {};
	sp.rest.v[0] = static_cast<S>(100);
	sp.rest.v[1] = static_cast<S>(80);
	sp.y = sp.rest.v[1];

	// El pie cae sobre el platillo: comprime y queda apoyado.
	Vec<2, S> feet {};
	feet.v[0] = static_cast<S>(100);
	feet.v[1] = static_cast<S>(80);
	S vy = static_cast<S>(5);
	spring_update(sp, feet, vy, static_cast<S>(4));
	CHECK(sp.pressed);
	CHECK(sp.y == static_cast<S>(85));      // comprimido 5 px
	CHECK(feet.v[1] == static_cast<S>(85)); // el pie se apoya
	CHECK(vy == static_cast<S>(0));

	// El pie se va: suelta con impulso hacia arriba (boost + compresión/2).
	feet.v[1] = static_cast<S>(60);
	vy = static_cast<S>(0);
	spring_update(sp, feet, vy, static_cast<S>(4));
	CHECK(!sp.pressed);
	CHECK(vy < static_cast<S>(0));

	// El platillo retorna a la posición de reposo.
	for (eng::u8 t = 0u; t < 60u; ++t) {
		Vec<2, S> idle {};
		idle.v[0] = static_cast<S>(0); // lejos de la ventana
		idle.v[1] = static_cast<S>(0);
		S idle_vy = static_cast<S>(0);
		spring_update(sp, idle, idle_vy, static_cast<S>(4));
	}
	CHECK(sp.y == sp.rest.v[1] && sp.vy == static_cast<S>(0));
}

template <typename S>
void test_catapult() {
	using eng::scene::Catapult;
	using eng::scene::catapult_fire;
	using eng::scene::catapult_load;
	using eng::scene::catapult_tip;
	using eng::scene::catapult_update;

	Catapult<S> c {};
	c.pivot.v[0] = static_cast<S>(200);
	c.pivot.v[1] = static_cast<S>(150);
	c.arm_length = static_cast<S>(30);

	// Carga: el brazo baja hasta `load_angle`.
	catapult_load(c);
	for (eng::u8 t = 0u; t < 80u; ++t) {
		catapult_update(c);
	}
	CHECK(c.angle == c.load_angle && c.loaded);

	// Disparo único: sube hasta `max_angle` y se para (el bucle sale al parar; después el
	// brazo ya volvería hacia el reposo).
	catapult_fire(c);
	CHECK(c.firing && !c.loaded);
	for (eng::u8 t = 0u; t < 20u && c.firing; ++t) {
		catapult_update(c);
	}
	CHECK(!c.firing && c.angle == c.max_angle);

	// Extremo exacto en los ángulos notables (tabla de seno: 64 = 90°).
	Catapult<S> tip_c {};
	tip_c.pivot.v[0] = static_cast<S>(100);
	tip_c.pivot.v[1] = static_cast<S>(100);
	tip_c.arm_length = static_cast<S>(30);
	Vec<2, S> tip {};
	tip_c.angle = 0;
	catapult_tip(tip_c, tip);
	CHECK(tip.v[0] == static_cast<S>(130) && tip.v[1] == static_cast<S>(100)); // derecha
	tip_c.angle = 64;
	catapult_tip(tip_c, tip);
	CHECK(tip.v[0] == static_cast<S>(100) && tip.v[1] == static_cast<S>(70)); // arriba
	tip_c.angle = -64;
	catapult_tip(tip_c, tip);
	CHECK(tip.v[0] == static_cast<S>(100) && tip.v[1] == static_cast<S>(130)); // abajo

	// Disparar sin cargar no hace nada.
	Catapult<S> c2 {};
	c2.pivot = c.pivot;
	catapult_fire(c2);
	CHECK(!c2.firing && !c2.loaded);
}

} // namespace

int main() {
	std::printf("Test HOST-438 rope_mechanisms (F6: cuerda, muelle, catapulta)\n");
	std::printf("===============================================================\n");

	std::printf("cuerda <s32>: determinismo, anclajes, restricciones, impulso y contacto\n");
	test_rope<eng::s32>();
	std::printf("cuerda <float>: misma formula con otro escalar\n");
	test_rope<float>();
	std::printf("muelle <s32> y <float>\n");
	test_spring<eng::s32>();
	test_spring<float>();
	std::printf("catapulta <s32>, <s16> y <float>\n");
	test_catapult<eng::s32>();
	test_catapult<eng::s16>();
	test_catapult<float>();

	if (g_failures == 0) {
		std::printf("OK: cuerda y mecanismos validados (s32/s16/float).\n");
		return 0;
	}
	std::printf("FAIL: %d comprobacion(es) fallaron\n", g_failures);
	return 1;
}
