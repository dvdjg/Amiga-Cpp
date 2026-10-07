#pragma once

/// \file trajectory.hpp
/// **Trayectorias y rutas** para objetos de juego (F2 de
/// `ROADMAP_JUEGO_SPRITES_BOBS.md` §5): polilíneas cocinadas con duración por punto,
/// **splines** Bézier evaluables en runtime (reutilizando `core/math/spline.hpp`) y un
/// **seguidor** determinista por ticks de juego.
///
/// El **seguimiento es genérico sobre el escalar** `S` (patrón del repo: `Vec<2,S>` +
/// `scalar_traits`, vale para `s16`, `s32`, `Fixed` o `float`); los **generadores** de
/// puntos trabajan en la escala del juego (enteros `s16`, coordenadas de pantalla) porque
/// son contenido cocinado/`constexpr`.
///
/// La polilínea controla la **velocidad** de forma explícita (ticks por punto); la spline
/// controla la **forma** (posición y tangente). Un asset puede combinar ambas: cocer la
/// spline a puntos con la tabla de longitud de arco deseada y usar la polilínea resultante.
///
/// Lógica pura (sin hardware, sin heap, sin STL): host-testable.

#include <eng/core/math/linalg.hpp>
#include <eng/core/math/scalar.hpp>
#include <eng/core/math/sinetable.hpp>
#include <eng/core/math/spline.hpp>
#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>

namespace eng::scene {

/// Punto de una trayectoria: offset (relativo al origen si `Trajectory::relative`) y
/// duración en ticks de juego hasta el siguiente punto.
template <typename S>
struct PathPoint {
	eng::math::Vec<2, S> p {};
	u16 ticks = 1;  ///< duración (>= 1); 0 se trata como 1
};

/// Trayectoria como tabla de puntos. `relative`: los puntos son offsets respecto al origen
/// del seguidor; si no, son posiciones absolutas de mundo.
template <typename S>
struct Trajectory {
	eng::Span<const PathPoint<S>> points {};
	bool loop = false;
	bool relative = true;
};

/// Estado de un objeto que sigue una trayectoria.
template <typename S>
struct TrajectoryFollower {
	u16 point_index = 0;
	u16 elapsed = 0;  ///< ticks consumidos del punto actual
	eng::math::Vec<2, S> origin {};  ///< punto de spawn (para `relative`)
	bool finished = false;
};

/// Avanza el seguidor `ticks` ticks y escribe la posición resultante en `out`. Con `ticks`
/// 0 (p. ej. al spawnear) devuelve la posición actual sin consumir tiempo. Al terminar una
/// trayectoria sin `loop`, queda `finished` en el último punto.
template <typename S>
inline void trajectory_advance(const Trajectory<S>& traj, TrajectoryFollower<S>& f, u16 ticks,
			       eng::math::Vec<2, S>& out) noexcept {
	if (traj.points.empty()) {
		out = f.origin;
		return;
	}
	if (f.finished) {
		out = traj.relative ? f.origin + traj.points[f.point_index].p : traj.points[f.point_index].p;
		return;
	}
	eng::u32 remaining = ticks;
	while (remaining > 0u) {
		const PathPoint<S>& p = traj.points[f.point_index];
		const u16 dur = p.ticks != 0u ? p.ticks : 1u;
		const u16 left = static_cast<u16>(dur - f.elapsed);
		if (remaining < left) {
			f.elapsed = static_cast<u16>(f.elapsed + remaining);
			break;
		}
		remaining -= left;
		f.elapsed = 0;
		if (static_cast<eng::u32>(f.point_index) + 1u < traj.points.size()) {
			++f.point_index;
		} else if (traj.loop) {
			f.point_index = 0;
		} else {
			f.finished = true;
			break;
		}
	}
	const PathPoint<S>& cur = traj.points[f.point_index];
	out = traj.relative ? f.origin + cur.p : cur.p;
}

/// **Ruta spline** de Bézier cúbicas encadenadas: `controls` lleva `1 + 3k` puntos
/// (`p0, c0, c1, p3, c0, c1, p3, …`), el formato que consume `math::bezier3`. La vista es
/// **no propietaria**: el llamador puede mover los puntos de control en runtime (la ruta
/// cambia en marcha, p. ej. desde una pista de timeline).
template <typename S>
struct RouteSpline {
	eng::Span<const eng::math::Vec<2, S>> controls {};
	[[nodiscard]] constexpr u16 segment_count() const noexcept {
		return controls.size() >= 4u ? static_cast<u16>((controls.size() - 1u) / 3u) : 0u;
	}
};

/// Muestrea la ruta en `t` global (0..1). Si la ruta no tiene tramos, devuelve el primer
/// punto (o cero si está vacía).
template <typename S>
[[nodiscard]] inline eng::math::Vec<2, S> route_sample(const RouteSpline<S>& r, S t) noexcept {
	using eng::math::Vec;
	const u16 segs = r.segment_count();
	if (segs == 0u) {
		return r.controls.empty() ? Vec<2, S>::zero() : r.controls[0];
	}
	const S scaled = t * eng::math::scalar_traits<S>::from_int(segs);
	eng::s32 k = eng::math::scalar_traits<S>::to_int(scaled);
	if (k < 0) {
		k = 0;
	} else if (k >= static_cast<eng::s32>(segs)) {
		k = static_cast<eng::s32>(segs - 1u);
	}
	const S local = scaled - eng::math::scalar_traits<S>::from_int(k);
	const eng::math::Vec<2, S>* const c = r.controls.data() + k * 3;
	return eng::math::bezier3(c[0], c[1], c[2], c[3], local);
}

/// **Tangente** (derivada) de la ruta en `t`: dirección de avance, base para orientar
/// naves/proyectiles. `B'(u) = 3(1−u)²(p1−p0) + 6(1−u)u(p2−p1) + 3u²(p3−p2)`.
template <typename S>
[[nodiscard]] inline eng::math::Vec<2, S> route_tangent(const RouteSpline<S>& r, S t) noexcept {
	using eng::math::mul_norm;
	using eng::math::Vec;
	const u16 segs = r.segment_count();
	if (segs == 0u) {
		return Vec<2, S>::zero();
	}
	const S scaled = t * eng::math::scalar_traits<S>::from_int(segs);
	eng::s32 k = eng::math::scalar_traits<S>::to_int(scaled);
	if (k < 0) {
		k = 0;
	} else if (k >= static_cast<eng::s32>(segs)) {
		k = static_cast<eng::s32>(segs - 1u);
	}
	const S u = scaled - eng::math::scalar_traits<S>::from_int(k);
	const S one = eng::math::scalar_traits<S>::one();
	const S three = eng::math::scalar_traits<S>::from_int(3);
	const S six = eng::math::scalar_traits<S>::from_int(6);
	const eng::math::Vec<2, S>* const c = r.controls.data() + k * 3;
	const S omu = one - u;
	const Vec<2, S> d = (c[1] - c[0]) * mul_norm(mul_norm(three, omu), omu) +
			    (c[2] - c[1]) * mul_norm(mul_norm(six, omu), u) +
			    (c[3] - c[2]) * mul_norm(mul_norm(three, u), u);
	return d;
}

namespace trajectory_detail {

/// Tabla de seno entera para las trayectorias cocinadas (amplitud 127).
inline constexpr eng::SineTable<127, 256> kPathSine {};

} // namespace trajectory_detail

/// **Generador de línea** (s16, cocinado): `length` puntos de `(i*dx, i*dy)` con la misma
/// duración por punto. Devuelve cuántos escribió.
inline u16 gen_line(eng::Span<PathPoint<s16>> out, s16 dx, s16 dy, u16 length,
		    u16 ticks_per = 1u) noexcept {
	u16 n = 0u;
	for (u16 i = 0u; i < length && n < out.size(); ++i) {
		out[n].p.v[0] = static_cast<s16>(i * dx);
		out[n].p.v[1] = static_cast<s16>(i * dy);
		out[n].ticks = ticks_per != 0u ? ticks_per : 1u;
		++n;
	}
	return n;
}

/// **Generador de seno vertical** (s16, cocinado): desplazamiento en X de amplitud
/// `amplitude` (tabla 4.12 reutilizada del engine) y avance en Y de `y_step` por punto.
/// `phase0` (0..255) permite desfasar brazos de una formación. Devuelve cuántos escribió.
inline u16 gen_sine_vertical(eng::Span<PathPoint<s16>> out, s16 amplitude, s16 y_step,
			     u16 length, u8 phase0 = 0u, u16 ticks_per = 1u) noexcept {
	u16 n = 0u;
	if (out.empty()) {
		return 0u;
	}
	for (u16 i = 0u; i < length && n < out.size(); ++i) {
		const u8 phase = static_cast<u8>(phase0 + static_cast<u8>((static_cast<eng::u32>(i) *
									    256u) /
									   (length != 0u ? length : 1u)));
		const eng::s32 s = trajectory_detail::kPathSine[phase];
		out[n].p.v[0] = static_cast<s16>((s * amplitude) / 127);
		out[n].p.v[1] = static_cast<s16>(i * y_step);
		out[n].ticks = ticks_per != 0u ? ticks_per : 1u;
		++n;
	}
	return n;
}

} // namespace eng::scene
