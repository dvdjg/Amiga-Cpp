#pragma once

/// \file mechanisms.hpp
/// **Mecanismos de escenario** (F6 de `ROADMAP_SPRITES_COMPLETO.md` §3.6): **muelle** de
/// compresión con retorno amortiguado y **catapulta** de brazo con estados de carga y
/// disparo (el extremo se calcula con la tabla de seno del engine; el visual del brazo lo
/// resuelve el juego, p. ej. con secuencias de un `CompositeVisual`).
///
/// Genérico sobre el escalar `S` de las posiciones (patrón del repo: `Vec<2,S>`,
/// `scalar_traits`, `div_norm`); el **ángulo** de la catapulta es `s16` en unidades de
/// tabla (256 = vuelta; 0 = horizontal derecha, positivo = arriba).
///
/// Puertas, destructibles y sólidos **no** se reimplementan: tienen diseño y primitivas en
/// `PIXEL_ART_2D_ISOMETRIC.md` §7 y `core/util/collision.hpp`.
///
/// Lógica pura (sin hardware, sin heap, sin STL): host-testable.

#include <eng/core/math/linalg.hpp>
#include <eng/core/math/scalar.hpp>
#include <eng/core/math/sinetable.hpp>
#include <eng/core/types/types.hpp>

namespace eng::scene::mechanism_detail {

/// Tabla de seno de la catapulta (amplitud 127; `sine_scaled` divide entre 127).
inline constexpr eng::SineTable<127, 256> kSine {};

} // namespace eng::scene::mechanism_detail

namespace eng::scene {

/// Muestras de seno escaladas a una amplitud entera: `sin(2π·phase/256) · amp`.
[[nodiscard]] inline eng::s32 sine_scaled(eng::u8 phase, eng::s32 amp) noexcept {
	return (mechanism_detail::kSine[phase] * amp) / 127;
}

/// **Muelle** (jump pad): platillo que se comprime con el peso y devuelve el impulso al
/// soltarse, con retorno amortiguado a la posición de reposo.
template <typename S>
struct Spring {
	eng::math::Vec<2, S> rest {}; ///< posición del platillo en reposo (x,y)
	S y {};                       ///< y actual del platillo
	S vy {};                      ///< velocidad del platillo
	S stiffness = 32;             ///< 0..256 (rigidez del retorno)
	S damping = 200;              ///< 0..256 (256 = sin amortiguación)
	S max_compress = 12;          ///< compresión máxima (px)
	S boost = 8;                  ///< impulso de salida (px/tick)
	bool pressed = false;
};

/// Contacto de los pies con el muelle + retorno. Actualiza `feet`/`feet_vy` (el pie se
/// apoya en el platillo y sale con `boost`). `half_w` = media anchura de los pies.
template <typename S>
inline void spring_update(Spring<S>& s, eng::math::Vec<2, S>& feet, S& feet_vy, S half_w) noexcept {
	using eng::math::scalar_traits;
	const S zero = scalar_traits<S>::zero();
	// Ventana de contacto: ±8 px en X sobre el platillo y [y-2, y+4] en Y (a juego con el
	// ancho del sprite del platillo).
	const S pad_x = scalar_traits<S>::from_int(8);
	const bool on = (feet.v[0] + half_w > static_cast<S>(s.rest.v[0] - pad_x)) &&
			(feet.v[0] - half_w < static_cast<S>(s.rest.v[0] + pad_x)) &&
			(feet.v[1] >= static_cast<S>(s.y - scalar_traits<S>::from_int(2))) &&
			(feet.v[1] <= static_cast<S>(s.y + scalar_traits<S>::from_int(4)));

	if (on && feet_vy >= zero) {
		s.pressed = true;
		s.y = static_cast<S>(s.y + feet_vy); // el peso comprime
		const S limit = static_cast<S>(s.rest.v[1] + s.max_compress);
		if (s.y > limit) {
			s.y = limit;
		}
		feet.v[1] = s.y;
		feet_vy = zero;
	} else if (s.pressed) {
		s.pressed = false; // suelta: impulso proporcional a la compresión
		feet_vy = static_cast<S>(-s.boost -
					 eng::math::div_norm<S>(static_cast<S>(s.y - s.rest.v[1]),
								scalar_traits<S>::from_int(2)));
		s.vy = static_cast<S>(eng::math::div_norm<S>(-s.boost,
							     scalar_traits<S>::from_int(2)));
	}
	if (on) {
		return; // el peso mantiene el platillo: sin retorno amortiguado mientras está pisado
	}

	// Retorno amortiguado hacia `rest.y` (spring-damper de un paso).
	const S force = eng::math::div_norm<S>(
		static_cast<S>((s.rest.v[1] - s.y) * s.stiffness -
			       static_cast<S>(s.vy * static_cast<S>(256 - s.damping))),
		scalar_traits<S>::from_int(256));
	s.vy = static_cast<S>(s.vy + force);
	s.y = static_cast<S>(s.y + s.vy);
	if ((s.vy > zero && s.y >= s.rest.v[1]) || (s.vy < zero && s.y <= s.rest.v[1])) {
		s.y = s.rest.v[1];
		s.vy = zero;
	}
}

/// **Catapulta** de brazo: pivote + longitud + ángulo (unidades de tabla) con estados de
/// carga y disparo. El visual del brazo (y del proyectil en el extremo) lo materializa el
/// juego; aquí vive la máquina de estado y el **extremo** analítico.
template <typename S>
struct Catapult {
	eng::math::Vec<2, S> pivot {}; ///< pivote del brazo
	S arm_length {};               ///< distancia pivote → cuchara (px)
	s16 angle = 0;                 ///< 0 = horizontal derecha; negativo = cargada; positivo = arriba
	s16 angle_speed = 0;
	s16 load_angle = -48; ///< ángulo de carga (≈ -67,5°)
	s16 max_angle = 72;   ///< fin del arco de disparo (≈ 101°)
	s16 fire_speed = 14;  ///< velocidad angular al disparar (unidades/tick)
	bool loaded = false;
	bool firing = false;
};

/// Deja el brazo cargado (quieto en `load_angle`).
template <typename S>
inline void catapult_load(Catapult<S>& c) noexcept {
	c.loaded = true;
	c.firing = false;
	c.angle_speed = 0;
}

/// Dispara si estaba cargada (un solo disparo; `catapult_update` lo para en `max_angle`).
template <typename S>
inline void catapult_fire(Catapult<S>& c) noexcept {
	if (!c.loaded) {
		return;
	}
	c.loaded = false;
	c.firing = true;
	c.angle_speed = c.fire_speed;
}

/// Avanza el brazo: disparo hacia `max_angle` o retorno suave a la carga/reposo.
template <typename S>
inline void catapult_update(Catapult<S>& c) noexcept {
	if (c.firing) {
		c.angle = static_cast<s16>(c.angle + c.angle_speed);
		if (c.angle >= c.max_angle) {
			c.angle = c.max_angle;
			c.firing = false;
			c.angle_speed = 0;
		}
		return;
	}
	const s16 target = c.loaded ? c.load_angle : 0;
	if (c.angle < target) {
		c.angle = static_cast<s16>(c.angle + 1);
	}
	if (c.angle > target) {
		c.angle = static_cast<s16>(c.angle - 1);
	}
}

/// Extremo del brazo (donde va el proyectil): `x = pivot.x + arm·cos(angle)`,
/// `y = pivot.y - arm·sin(angle)` con la tabla de seno (0 = derecha, positivo = arriba).
template <typename S>
inline void catapult_tip(const Catapult<S>& c, eng::math::Vec<2, S>& out) noexcept {
	const eng::s32 arm = eng::math::scalar_traits<S>::to_int(c.arm_length);
	const u8 a = static_cast<u8>(c.angle);
	out.v[0] = static_cast<S>(c.pivot.v[0] + sine_scaled(static_cast<u8>(a + 64u), arm));
	out.v[1] = static_cast<S>(c.pivot.v[1] - sine_scaled(a, arm));
}

} // namespace eng::scene
