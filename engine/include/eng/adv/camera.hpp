#pragma once

/// \file camera.hpp
/// **Cámara de mundo** (`eng::adv`, hito A0): posición en coordenadas de mundo con límites,
/// **dead-zone** (zona muerta), seguimiento de un punto (actor), paneos temporizados y
/// trayectoria por keyframes con easing polinómico. Lógica pura con fixed-point genérico
/// (parámetro `S`), sin estado oculto ni heap; el compositor (A3) la convierte en offsets de
/// scroll por capa (parallax) y el juego solo le pasa la posición del objetivo.
///
/// Convenio: `pos` es el **origen superior-izquierdo** de la vista (coordenadas de mundo);
/// `world_min`/`world_max` acotan ese origen (el juego resta el tamaño del viewport).
/// `soft_min`/`soft_max` definen la zona muerta en mundo: si el objetivo está dentro, la
/// cámara no se mueve; si sale, la cámara se desplaza lo justo para que quede en el borde.
///
/// Easing: `smoothstep` (polinómico) — seguro para cualquier escalar sin división por tablas
/// (`interp.hpp`); el `lerp` usa `mul_norm` cuando el escalar lo aporta.

#include <eng/core/math/interp.hpp>
#include <eng/core/math/linalg.hpp>
#include <eng/core/types/types.hpp>

namespace eng::adv {

/// Cámara de mundo genérica sobre el escalar `S` (coordenadas en píxeles o `Coord`).
/// \tparam S escalar de coordenada con **fracciones normalizadas** (`Fixed`/`float`); los
/// paneos usan `inv_lerp`/`mul_norm`/`smoothstep` (no usar un entero crudo para `S`).
template <class S>
struct Camera {
	eng::math::Vec<2, S> pos {};        ///< origen de la vista (mundo)
	eng::math::Vec<2, S> target {};     ///< punto seguido (p. ej. actor)
	eng::math::Vec<2, S> world_min {};  ///< mínimo permitido de `pos`
	eng::math::Vec<2, S> world_max {};  ///< máximo permitido de `pos`
	eng::math::Vec<2, S> soft_min {};   ///< zona muerta (mundo)
	eng::math::Vec<2, S> soft_max {};   ///< zona muerta (mundo)
	bool has_bounds {false};            ///< aplicar `world_min/max`
	bool has_soft {false};              ///< aplicar dead-zone
	bool following {false};             ///< seguir `target` en cada `update`
	eng::math::Vec<2, S> pan_from {};   ///< origen del paneo en curso
	eng::math::Vec<2, S> pan_dest {};     ///< destino del paneo
	eng::u16 pan_frames {0};            ///< duración total del paneo
	eng::u16 pan_left {0};              ///< frames restantes

	/// Fija los límites del origen de la vista (mundo).
	/// \param min esquina mínima permitida.
	/// \param max esquina máxima permitida.
	constexpr void set_bounds(eng::math::Vec<2, S> min, eng::math::Vec<2, S> max) noexcept {
		world_min = min;
		world_max = max;
		has_bounds = true;
	}

	/// Fija la zona muerta (rectángulo de mundo donde el objetivo puede moverse sin desplazar
	/// la cámara).
	/// \param min esquina mínima de la zona muerta.
	/// \param max esquina máxima de la zona muerta.
	constexpr void set_soft_bounds(eng::math::Vec<2, S> min, eng::math::Vec<2, S> max) noexcept {
		soft_min = min;
		soft_max = max;
		has_soft = true;
	}

	/// Coloca la cámara de inmediato (sin paneo), respetando límites.
	/// \param p nuevo origen de la vista.
	constexpr void place(eng::math::Vec<2, S> p) noexcept {
		pos = clamp_to(p);
		pan_left = 0;
	}

	/// Activa/actualiza el seguimiento del punto `p` (p. ej. el actor del jugador).
	/// \param p punto de mundo a seguir.
	constexpr void follow(eng::math::Vec<2, S> p) noexcept {
		target = p;
		following = true;
	}

	/// Desactiva el seguimiento (cutscene/cámara fija).
	constexpr void stop_follow() noexcept { following = false; }

	/// Inicia un paneo suave hasta `p` durante `frames` (0 = instantáneo).
	/// \param p destino del origen de la vista.
	/// \param frames duración en frames de juego.
	constexpr void pan_to(eng::math::Vec<2, S> p, eng::u16 frames) noexcept {
		if (frames == 0) {
			place(p);
			return;
		}
		pan_from = pos;
		pan_dest = clamp_to(p);
		pan_frames = frames;
		pan_left = frames;
	}

	/// ¿Hay un paneo en curso?
	/// \return `true` mientras queden frames de paneo.
	[[nodiscard]] constexpr bool panning() const noexcept { return pan_left > 0; }

	/// Avanza la cámara un tick de juego (llamar una vez por frame lógico).
	constexpr void update() noexcept {
		if (pan_left > 0) {
			pan_left--;
			const S t = eng::math::inv_lerp(eng::math::scalar_traits<S>::zero(),
							eng::math::scalar_traits<S>::from_int(pan_frames),
							eng::math::scalar_traits<S>::from_int(pan_frames - pan_left));
			const S e = eng::math::smoothstep(t);
			pos.v[0] = static_cast<S>(pan_from.v[0] + eng::math::mul_norm(pan_dest.v[0] - pan_from.v[0], e));
			pos.v[1] = static_cast<S>(pan_from.v[1] + eng::math::mul_norm(pan_dest.v[1] - pan_from.v[1], e));
			if (pan_left == 0) pos = clamp_to(pan_dest);
			return;
		}
		if (!following) return;
		eng::math::Vec<2, S> desired = pos;
		if (has_soft) {
			// Si el objetivo sale de la zona muerta, desplazar la cámara lo justo.
			const S cx = clamp_axis(target.v[0], soft_min.v[0], soft_max.v[0]);
			const S cy = clamp_axis(target.v[1], soft_min.v[1], soft_max.v[1]);
			desired.v[0] = static_cast<S>(desired.v[0] + (target.v[0] - cx));
			desired.v[1] = static_cast<S>(desired.v[1] + (target.v[1] - cy));
		} else {
			desired = target;
		}
		pos = clamp_to(desired);
	}

private:
	[[nodiscard]] static constexpr S clamp_axis(S x, S lo, S hi) noexcept {
		return x < lo ? lo : (x > hi ? hi : x);
	}
	[[nodiscard]] constexpr eng::math::Vec<2, S> clamp_to(eng::math::Vec<2, S> p) const noexcept {
		if (!has_bounds) return p;
		return eng::math::Vec<2, S> {{clamp_axis(p.v[0], world_min.v[0], world_max.v[0]),
					      clamp_axis(p.v[1], world_min.v[1], world_max.v[1])}};
	}
};

} // namespace eng::adv
