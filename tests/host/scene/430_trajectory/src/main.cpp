// ============================================================================
// Test HOST-430: trayectorias, splines, formaciones y pool de entidades - F2.
// ============================================================================
//
// Respalda `eng/scene/{trajectory,formation,entity_pool}.hpp`: seguimiento genérico sobre
// el escalar (s32 y float), polilínea con duración por punto (loop/finished/absoluta),
// muestreo de ruta Bézier y tangente, oleadas con delay y pool de entidades (spawn,
// trayectoria, animación, culling, fin de proyectil y pool lleno).
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/scene/430_trajectory

#include <cstdio>
#include <cmath>

#include <eng/scene/entity_pool.hpp>
#include <eng/scene/formation.hpp>
#include <eng/scene/trajectory.hpp>

namespace {

int g_fail = 0;
void check(bool ok, const char* m) {
	if (!ok) {
		std::printf("[FAIL] %s\n", m);
		++g_fail;
	}
}

using eng::math::Vec;

/// Recorrido genérico: mismo algoritmo con `s32` y con `float` (la cabecera no fija escalar).
template <typename S>
void test_core_trajectory() {
	using eng::scene::PathPoint;
	using eng::scene::Trajectory;
	using eng::scene::TrajectoryFollower;
	using eng::scene::trajectory_advance;

	const auto make = [](S x, S y) {
		Vec<2, S> v {};
		v.x() = x;
		v.y() = y;
		return v;
	};
	const PathPoint<S> pts[3] = {
		{Vec<2, S>::zero(), 1u},
		{make(S(10), S(0)), 2u},
		{make(S(10), S(10)), 1u},
	};
	const Trajectory<S> traj {eng::Span<const PathPoint<S>> {pts, 3u}, false, true};
	TrajectoryFollower<S> f {};
	f.origin.x() = S(100);
	f.origin.y() = S(50);

	Vec<2, S> out {};
	trajectory_advance(traj, f, 0u, out);
	check(out.x() == S(100) && out.y() == S(50), "spawn: primer punto relativo");

	trajectory_advance(traj, f, 1u, out);
	check(out.x() == S(110) && out.y() == S(50) && !f.finished, "1 tick: segundo punto");

	trajectory_advance(traj, f, 1u, out);
	check(out.x() == S(110) && !f.finished, "punto 1 dura 2 ticks");
	trajectory_advance(traj, f, 1u, out);
	check(out.x() == S(110) && out.y() == S(60) && !f.finished, "tercer punto");
	trajectory_advance(traj, f, 1u, out);
	check(out.x() == S(110) && out.y() == S(60) && f.finished,
	      "fin tras agotar la duracion del ultimo punto");

	// Loop: vuelve al primer punto indefinidamente.
	TrajectoryFollower<S> fl {};
	const Trajectory<S> loop {eng::Span<const PathPoint<S>> {pts, 3u}, true, true};
	for (int i = 0; i < 8; ++i) {
		trajectory_advance(loop, fl, 1u, out);
	}
	check(!fl.finished, "loop no termina");

	// Absoluta: los puntos son posiciones de mundo (el origen se ignora).
	TrajectoryFollower<S> fa {};
	const Trajectory<S> abs {eng::Span<const PathPoint<S>> {pts, 3u}, false, false};
	fa.origin.x() = S(999);
	trajectory_advance(abs, fa, 1u, out);
	check(out.x() == S(10) && out.y() == S(0), "absoluta ignora el origen");
}

/// Ruta Bézier cúbica: extremos exactos y punto medio conocido.
void test_route_spline() {
	using eng::scene::RouteSpline;
	using eng::scene::route_sample;
	using eng::scene::route_tangent;

	Vec<2, float> c[4] {};
	c[0] = {};
	c[1].x() = 0.0f;
	c[1].y() = 10.0f;
	c[2].x() = 10.0f;
	c[2].y() = 10.0f;
	c[3].x() = 10.0f;
	c[3].y() = 0.0f;
	const RouteSpline<float> r {eng::Span<const Vec<2, float>> {c, 4u}};
	check(r.segment_count() == 1u, "una cubica");

	const Vec<2, float> p0 = route_sample(r, 0.0f);
	const Vec<2, float> p1 = route_sample(r, 1.0f);
	check(p0.x() == 0.0f && p0.y() == 0.0f, "t=0 -> p0");
	check(p1.x() == 10.0f && p1.y() == 0.0f, "t=1 -> p3");
	const Vec<2, float> pm = route_sample(r, 0.5f);
	check(std::fabs(pm.x() - 5.0f) < 0.001f && std::fabs(pm.y() - 7.5f) < 0.001f,
	      "(p0+3p1+3p2+p3)/8 en t=0.5");

	// Tangente en t=0 apunta hacia p1 (hacia arriba) y en t=1 hacia p3-p2 (hacia abajo).
	const Vec<2, float> t0 = route_tangent(r, 0.0f);
	const Vec<2, float> t1 = route_tangent(r, 1.0f);
	check(t0.x() == 0.0f && t0.y() > 0.0f, "tangente inicial vertical");
	check(std::fabs(t1.x()) < 0.001f && t1.y() < 0.0f, "tangente final vertical (baja)");
}

/// Formación: delays 0,2,4; el cruce por ticks acumulados no pierde miembros.
void test_formation() {
	using eng::scene::Formation;
	using eng::scene::FormationMember;
	using eng::scene::FormationState;
	using eng::scene::formation_update;

	const FormationMember<eng::s16> members[3] = {
		{0u, {}, 0u},
		{2u, {10, 0}, 1u},
		{4u, {20, 0}, 2u},
	};
	const Formation<eng::s16> form {eng::Span<const FormationMember<eng::s16>> {members, 3u},
					 {100, 50}};
	FormationState st {};
	st.active = true;

	int spawned = 0;
	eng::u8 traj_seen[3] = {0, 0, 0};
	auto spawn = [&](eng::usize idx, eng::u8 traj, Vec<2, eng::s16> pos) {
		check(pos.x() == static_cast<eng::s16>(100 + idx * 10) && pos.y() == 50,
		      "offset del miembro aplicado");
		if (idx < 3u) {
			traj_seen[idx] = traj;
		}
		++spawned;
	};

	formation_update(form, st, 1u, spawn);
	check(spawned == 1 && st.age == 1u, "delay 0 sale en el primer update");
	formation_update(form, st, 1u, spawn);
	check(spawned == 1, "delay 2 aun espera");
	formation_update(form, st, 1u, spawn);
	check(spawned == 2, "delay 2 sale (2 ticks de espera)");
	formation_update(form, st, 1u, spawn);
	check(spawned == 2, "delay 4 aun espera");
	formation_update(form, st, 1u, spawn);
	check(spawned == 3 && traj_seen[0] == 0u && traj_seen[1] == 1u && traj_seen[2] == 2u,
	      "delay 4 sale y cada miembro conserva su trayectoria");

	FormationState st2 {};
	st2.active = true;
	int burst = 0;
	formation_update(form, st2, 5u, [&](eng::usize, eng::u8, Vec<2, eng::s16>) { ++burst; });
	check(burst == 3, "salto de 5 ticks: los tres delays cruzan en una pasada");
}

/// Pool: spawn con trayectoria, avance, culling y fin de proyectil.
void test_entity_pool() {
	using eng::scene::EntityKind;
	using eng::scene::EntityPool;
	using eng::scene::PathPoint;
	using eng::scene::Trajectory;

	const PathPoint<eng::s16> bullet_pts[3] = {
		{{0, 0}, 1u},
		{{0, -8}, 1u},
		{{0, -16}, 1u},
	};
	const Trajectory<eng::s16> bullet {eng::Span<const PathPoint<eng::s16>> {bullet_pts, 3u},
					   false, true};
	const Trajectory<eng::s16> table[1] = {bullet};

	EntityPool<4> pool {};
	const eng::u16 b0 = pool.spawn(EntityKind::Projectile, nullptr, &bullet, 0u, 100, 200, 0u,
				       true);
	check(b0 == 0u && pool.live_count() == 1u, "spawn de proyectil");
	check(pool.slots[b0].y == 200 && pool.slots[b0].vertical_stream, "estado inicial");

	pool.update(eng::Span<const Trajectory<eng::s16>> {table, 1u}, 1u);
	check(pool.slots[b0].y == 192 && pool.slots[b0].prev_y == 200, "avance y prev");
	pool.update(eng::Span<const Trajectory<eng::s16>> {table, 1u}, 3u);
	check(pool.slots[b0].active == false, "proyectil al terminar se apaga");

	// Culling: una entidad quieta fuera del rectangulo visible.
	const eng::u16 far = pool.spawn(EntityKind::Enemy, nullptr, nullptr, 0u, 1000, 100);
	check(far != 0xffffu, "spawn sin trayectoria");
	pool.update(eng::Span<const Trajectory<eng::s16>> {table, 1u}, 1u);
	check(pool.slots[far].active == false, "culling fuera de pantalla");

	// Pool lleno.
	for (eng::u16 i = 0; i < 4u; ++i) {
		(void)pool.spawn(EntityKind::Enemy, nullptr, nullptr, 0u, 10, 10);
	}
	check(pool.live_count() == 4u, "pool lleno");
	check(pool.spawn(EntityKind::Enemy, nullptr, nullptr, 0u, 10, 10) == 0xffffu,
	      "sin hueco -> 0xffff");
}

} // namespace

int main() {
	std::printf("== HOST-430 trajectory ==\n");

	test_core_trajectory<eng::s32>();
	test_core_trajectory<float>();
	test_route_spline();
	test_formation();
	test_entity_pool();

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: trayectorias, splines, formaciones y pool validados.\n");
	return 0;
}
