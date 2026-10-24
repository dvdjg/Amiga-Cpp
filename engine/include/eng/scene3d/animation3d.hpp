#pragma once

/// \file animation3d.hpp
/// **Animación rígida de nodos** para la API 3D (G4): clips de ticks con tracks de
/// posición y rotación, interpolación lineal/shortest-turns, loop/one-shot y eventos.
///
/// Es deliberadamente pequeño y sin heap: el llamador aporta arrays estáticos de
/// keyframes/tracks/eventos y un `Player` de capacidad fija. No implementa skinning
/// por vértice, morph targets ni clips de asset `Animation3D`; esas piezas llegan
/// cuando el asset runtime las cargue. Para un personaje de piezas rígidas, aplicar
/// `Pose::rotation` a los nodos de la jerarquía ya da animación de juego.

#include <eng/core/data/mesh3d.hpp>
#include <eng/core/math/linalg.hpp>
#include <eng/core/math/scalar.hpp>
#include <eng/core/math/scalar_math.hpp>
#include <eng/core/types/ptr.hpp>
#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>

namespace eng::anim3d {

/// Fracción Q4.12 a escalar real. En `Fixed<...,12>` el valor es el crudo (4096 =
/// una vuelta); en coma flotante es `q / 4096`. No usa `from_int(4096)`, que en
/// Q4.12 desborda y congelaba la interpolación.
/// \tparam R escalar de rotación; `Fixed` de 16 bits usa el crudo y el resto divide.
template <class R>
struct RatioQ12 {
	static R get(u16 q) noexcept {
		return static_cast<R>(q) / static_cast<R>(4096);
	}
};
/// Especialización para fixed de 16 bits sin división.
/// \tparam R escalar fixed de 16 bits (Q4.12).
template <class R>
	requires (sizeof(R) == 2u && !eng::math::numeric_traits<R>::has_division)
struct RatioQ12<R> {
	static R get(u16 q) noexcept {
		return R {static_cast<s16>(q)};
	}
};


/// Keyframe de un track. `tick` es absoluto dentro del clip (0..duration).
struct Keyframe {
	u16 tick = 0; ///< tick del keyframe.
	math3d::Vec3t<eng::coord> translation {}; ///< traslación local en coordenadas de escena.
	math3d::Vec3t<eng::real> rotation {}; ///< rotación local en turns (0..1).
};

/// Track de un objetivo lógico (`target` lo mapea el juego a un `NodeId`).
struct Track {
	u16 target = 0; ///< índice de objetivo del juego.
	u16 first = 0; ///< primer keyframe en `Clip::keys`.
	u16 count = 0; ///< número de keyframes (>=1).
};

/// Evento de gameplay asociado a un tick del clip.
struct Event {
	u16 tick = 0; ///< tick en el que cruza el evento.
	u16 id = 0; ///< id opaco para el juego.
};

/// Clip de animación no propietario; los spans deben sobrevivir mientras se reproduce.
struct Clip {
	eng::Span<const Keyframe> keys {}; ///< keyframes de todos los tracks.
	eng::Span<const Track> tracks {}; ///< tracks del clip.
	eng::Span<const Event> events {}; ///< eventos opcionales.
	u16 duration = 0; ///< duración en ticks (>0).
	bool loop = true; ///< true = loop; false = one-shot que se queda al final.
};

/// Pose muestreada de un track.
struct Pose {
	math3d::Vec3t<eng::coord> translation {}; ///< traslación local.
	math3d::Vec3t<eng::real> rotation {}; ///< rotación local en turns.
};

/// Reproductor de un clip, capacidad fija.
/// \tparam MaxTracks tracks máximos consultables (>0).
/// \tparam MaxEvents eventos pendientes máximos (>0).
template <u16 MaxTracks, u16 MaxEvents = 8u>
class Player {
	static_assert(MaxTracks > 0u && MaxEvents > 0u, "Player: capacidades > 0");

public:
	/// \return true si hay un clip activo.
	[[nodiscard]] bool playing() const noexcept { return m_clip.valid(); }
	/// \return tick actual dentro del clip.
	[[nodiscard]] u16 tick() const noexcept { return m_tick; }
	/// \return número de eventos pendientes.
	[[nodiscard]] u16 pending_events() const noexcept { return m_event_count; }

	/// Inicia un clip en el tick 0.
	/// \param clip clip no propietario que debe sobrevivir a la reproducción.
	void play(const Clip& clip) noexcept {
		m_clip = clip;
		m_tick = 0;
		m_event_count = 0;
	}

	/// Detiene la reproducción sin liberar el clip.
	void stop() noexcept {
		m_clip.reset();
		m_tick = 0;
		m_event_count = 0;
	}

	/// Avanza `ticks` y encola los eventos cruzados.
	/// \param ticks ticks a avanzar (>0).
	/// \return true si el clip sigue activo.
	bool update(u16 ticks) noexcept {
		if (!m_clip.valid() || m_clip->duration == 0u || m_clip->tracks.empty()) {
			return false;
		}
		const u16 duration = m_clip->duration;
		if (ticks == 0u) return true;
		const u16 begin = m_tick;
		const u32 absolute = static_cast<u32>(begin) + ticks;
		if (m_clip->loop) {
			const u32 wraps = absolute / duration;
			m_tick = static_cast<u16>(absolute % duration);
			if (wraps == 0u) {
				queue_range(begin, m_tick);
			} else {
				queue_range(begin, duration);
				queue_range(0u, m_tick);
			}
		} else {
			m_tick = absolute >= duration ? duration : static_cast<u16>(absolute);
			queue_range(begin, m_tick);
		}
		return true;
	}

	/// Consume un evento pendiente.
	/// \param out id del evento.
	/// \return true si había uno.
	bool take_event(u16& out) noexcept {
		if (m_event_count == 0u) return false;
		out = m_events[m_event_head];
		m_event_head = static_cast<u16>((m_event_head + 1u) % MaxEvents);
		--m_event_count;
		return true;
	}

	/// Muestrea el track `index` en el tick actual.
	/// \param index índice de track en `Clip::tracks`.
	/// \param out pose resultante.
	/// \return false si el clip/índice no es válido.
	bool sample(u16 index, Pose& out) const noexcept {
		if (!m_clip.valid() || index >= m_clip->tracks.size()) return false;
		const Track& track = m_clip->tracks[index];
		if (track.count == 0u ||
		    static_cast<u32>(track.first) + track.count > m_clip->keys.size()) {
			return false;
		}
		const Keyframe* const keys = m_clip->keys.data();
		const Keyframe& first = keys[track.first];
		const Keyframe& last = keys[track.first + track.count - 1u];
		u16 t = m_tick;
		if (t < first.tick) t = first.tick;
		if (t > last.tick) t = last.tick;
		u16 k = 0u;
		while (k + 2u < track.count && keys[track.first + k + 1u].tick <= t) {
			++k;
		}
		const Keyframe& a = keys[track.first + k];
		const Keyframe& b = keys[track.first + (k + 1u < track.count ? k + 1u : k)];
		const u16 span = b.tick > a.tick ? static_cast<u16>(b.tick - a.tick) : 0u;
		const u16 ratio_q12 = span == 0u
			? 4096u
			: static_cast<u16>((static_cast<u32>(t - a.tick) * 4096u) / span);
		const eng::real ratio = RatioQ12<eng::real>::get(ratio_q12);
		out.translation = lerp_translation(a.translation, b.translation, ratio_q12);
		out.rotation = lerp_rotation(a.rotation, b.rotation, ratio);
		return true;
	}

private:
	using R = eng::math::scalar_traits<eng::real>;
	using T = eng::math::scalar_traits<eng::coord>;


	/// Interpolación entera de traslación q0 con `ratio_q12` Q4.12 (0..4096).
	static math3d::Vec3t<eng::coord> lerp_translation(
		const math3d::Vec3t<eng::coord>& a, const math3d::Vec3t<eng::coord>& b,
		u16 ratio_q12) noexcept {
		math3d::Vec3t<eng::coord> r {};
		const s32 k = ratio_q12;
		for (int i = 0; i < 3; ++i) {
			const s32 av = T::to_int(a.v[i]);
			const s32 bv = T::to_int(b.v[i]);
			r.v[i] = T::from_int(av + ((bv - av) * k) / 4096);
		}
		return r;
	}

	/// Interpolación shortest-turns; `ratio` Q4.12 en 0..1.
	static math3d::Vec3t<eng::real> lerp_rotation(const math3d::Vec3t<eng::real>& a,
						     const math3d::Vec3t<eng::real>& b,
						     eng::real ratio) noexcept {
		math3d::Vec3t<eng::real> r {};
		const eng::real half = eng::math::div_norm(R::one(), R::from_int(2));
		for (int i = 0; i < 3; ++i) {
			eng::real d = b.v[i] - a.v[i];
			if (d > half) d = d - R::one();
			if (d < -half) d = d + R::one();
			r.v[i] = a.v[i] + eng::math::mul_norm(d, ratio);
		}
		return r;
	}

	/// Encola los eventos con `lo < tick <= hi`.
	void queue_range(u16 lo, u16 hi) noexcept {
		if (!m_clip.valid() || m_clip->events.empty() || hi <= lo) return;
		for (u16 i = 0u; i < m_clip->events.size() && m_event_count < MaxEvents; ++i) {
			const Event& ev = m_clip->events[i];
			if (ev.tick > lo && ev.tick <= hi) {
				m_events[m_event_tail] = ev.id;
				m_event_tail = static_cast<u16>((m_event_tail + 1u) % MaxEvents);
				++m_event_count;
			}
		}
	}

	eng::Ref<const Clip> m_clip {};
	u16 m_tick = 0;
	u16 m_events[MaxEvents] {};
	u16 m_event_head = 0;
	u16 m_event_tail = 0;
	u16 m_event_count = 0;
};

/// Construye una matriz de rotación a partir de turns locales.
/// \tparam Real escalar de rotación (por defecto eng::real); dependiente para que la
///         especialización 	urns de sincos se resuelva al instanciar.
/// \param turns tres ángulos en turns.
/// \return matriz equivalente a Rx*Ry*Rz.
template <class Real = eng::real>
[[nodiscard]] inline eng::math::Mat<3, Real> rotation_from_turns(
	const math3d::Vec3t<Real>& turns) noexcept {
	Real sx {}, cx {}, sy {}, cy {}, sz {}, cz {};
	eng::math::sincos(eng::math::Angle<Real, eng::math::angle::turns> {turns.v[0]}, sx, cx);
	eng::math::sincos(eng::math::Angle<Real, eng::math::angle::turns> {turns.v[1]}, sy, cy);
	eng::math::sincos(eng::math::Angle<Real, eng::math::angle::turns> {turns.v[2]}, sz, cz);
	const Real t0 = eng::math::mul_norm(sy, cz);
	const Real t1 = eng::math::mul_norm(sy, sz);
	eng::math::Mat<3, Real> m {};
	m.m[0][0] = eng::math::mul_norm(cy, cz);
	m.m[0][1] = -eng::math::mul_norm(cy, sz);
	m.m[0][2] = sy;
	m.m[1][0] = eng::math::mul_norm(cx, sz) + eng::math::mul_norm(sx, t0);
	m.m[1][1] = eng::math::mul_norm(cx, cz) - eng::math::mul_norm(sx, t1);
	m.m[1][2] = -eng::math::mul_norm(sx, cy);
	m.m[2][0] = eng::math::mul_norm(sx, sz) - eng::math::mul_norm(cx, t0);
	m.m[2][1] = eng::math::mul_norm(sx, cz) + eng::math::mul_norm(cx, t1);
	m.m[2][2] = eng::math::mul_norm(cx, cy);
	return m;
}

} // namespace eng::anim3d
