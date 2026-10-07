#pragma once

/// \file sequence.hpp
/// **Secuenciador genérico de valores y eventos** (F8 de
/// `ROADMAP_JUEGO_SPRITES_BOBS.md` §5): pistas de **claves** con interpolación (posición,
/// escala, velocidad, paleta…) y pistas de **eventos** con tick, sobre una línea temporal
/// determinista en **ticks de juego** y con capacidad fija (sin heap).
///
/// Es un ladrillo de dominio genérico (no de IA ni de simulación): sirve para dirigir
/// oleadas de un shmup (spawns + rutas), cutscenes, cámara o UI. Las secuencias se cocinan
/// a `constexpr`/tablas desde un formato de autoría, como el resto de assets.
///
/// El **escalar del valor** es genérico (`float`, `Fixed`, `MiniFloat16`…): la
/// interpolación necesita fracción, así que para valores enteros usa `Ease::Step` o
/// claves que no interpoles entre medias.
///
/// Lógica pura (sin hardware, sin heap, sin STL): host-testable.

#include <eng/core/math/interp.hpp>
#include <eng/core/math/linalg.hpp>
#include <eng/core/math/scalar.hpp>
#include <eng/core/types/types.hpp>

namespace eng::util {

/// Interpolación de una clave hacia la siguiente.
enum class Ease : eng::u8 {
	Step = 0,   ///< se mantiene el valor de la clave hasta la siguiente
	Linear,     ///< interpolación lineal
	EaseIn,     ///< arranque suave (`ease_in_sine`)
	EaseOut,    ///< frenada suave (`ease_out_sine`)
	EaseInOut,  ///< suave en ambos extremos (`ease_in_out_sine`)
};

namespace sequence_detail {

/// Aplica la curva de `ease` a un factor `f` en `[0,1]` (se recorta).
template <typename S>
[[nodiscard]] constexpr S apply_ease(Ease ease, S f) noexcept {
	using eng::math::scalar_traits;
	const S zero = scalar_traits<S>::zero();
	const S one = scalar_traits<S>::one();
	if (f < zero) {
		f = zero;
	} else if (f > one) {
		f = one;
	}
	switch (ease) {
		case Ease::Step:
			return zero;
		case Ease::Linear:
			return f;
		case Ease::EaseIn:
			return eng::math::ease_in_sine(f);
		case Ease::EaseOut:
			return eng::math::ease_out_sine(f);
		case Ease::EaseInOut:
		default:
			return eng::math::ease_in_out_sine(f);
	}
}

} // namespace sequence_detail

/// **Pista de valores**: claves ordenadas por `tick` (el llamador las añade en orden
/// ascendente con `add`). `sample` interpola el valor en un tick dado.
template <typename S, eng::u8 MaxKeys>
struct KeyTrack {
	struct Key {
		eng::u16 tick = 0;
		S value {};
		Ease ease = Ease::Linear;
	};

	Key keys[MaxKeys] {};
	eng::u8 count = 0;

	/// Añade una clave al final si cabe (debe venir con tick >= la última).
	bool add(eng::u16 tick, S value, Ease ease = Ease::Linear) noexcept {
		if (count >= MaxKeys) {
			return false;
		}
		if (count != 0u && tick < keys[count - 1u].tick) {
			return false; // fuera de orden
		}
		keys[count++] = Key {tick, value, ease};
		return true;
	}

	/// Valor en `tick`: antes de la primera clave vale la primera; después de la última,
	/// la última; en medio, interpolación entre las dos claves que lo rodean.
	[[nodiscard]] S sample(eng::u32 tick) const noexcept {
		using eng::math::div_norm;
		using eng::math::lerp;
		using eng::math::scalar_traits;
		if (count == 0u) {
			return scalar_traits<S>::zero();
		}
		if (tick <= keys[0].tick) {
			return keys[0].value;
		}
		for (eng::u8 i = 1u; i < count; ++i) {
			if (tick < keys[i].tick) {
				const Key& a = keys[i - 1u];
				const Key& b = keys[i];
				const eng::u32 span = static_cast<eng::u32>(b.tick - a.tick);
				const S f = sequence_detail::apply_ease(
					b.ease,
					div_norm(scalar_traits<S>::from_int(
							 static_cast<int>(tick - a.tick)),
						 scalar_traits<S>::from_int(static_cast<int>(span))));
				return lerp(a.value, b.value, f);
			}
		}
		return keys[count - 1u].value;
	}
};

/// **Pista de eventos**: pares `(tick, id)` en orden ascendente; el `id` lo interpreta el
/// juego (spawn, música, cambio de estado…).
template <eng::u8 MaxEvents>
struct EventTrack {
	struct Event {
		eng::u16 tick = 0;
		eng::u16 id = 0;
	};

	Event events[MaxEvents] {};
	eng::u8 count = 0;

	bool add(eng::u16 tick, eng::u16 id) noexcept {
		if (count >= MaxEvents) {
			return false;
		}
		if (count != 0u && tick < events[count - 1u].tick) {
			return false; // fuera de orden
		}
		events[count++] = Event {tick, id};
		return true;
	}
};

/// **Secuencia**: `MaxTracks` pistas de valor (escalar `S`) y `MaxTracks` pistas de evento,
/// longitud en ticks y `loop`.
template <typename S, eng::u8 MaxTracks, eng::u8 MaxKeys, eng::u8 MaxEvents>
struct Sequence {
	static constexpr eng::u8 kMaxTracks = MaxTracks;

	KeyTrack<S, MaxKeys> values[MaxTracks] {};
	EventTrack<MaxEvents> events[MaxTracks] {};
	eng::u16 length = 0;
	bool loop = false;
};

/// **Reproductor** de una secuencia: estado mínimo y avance determinista por ticks. El
/// llamador spawnea/actúa en `on_event(id, tick)` y muestrea los valores con
/// `seq.values[track].sample(runner.tick)`.
struct SequenceRunner {
	eng::u32 tick = 0;
	bool playing = false;
	bool finished = false;

	void play() noexcept { playing = true; }
	void pause() noexcept { playing = false; }
	void reset(bool start_playing = true) noexcept {
		tick = 0u;
		finished = false;
		playing = start_playing;
	}

	/// Avanza `ticks` y dispara los eventos del intervalo. Con `loop`, una vuelta se
	/// cierra como máximo por avance (un salto mayor que `length` no dispara los eventos
	/// intermedios; usa avances de una vuelta o menos).
	template <typename S, eng::u8 MT, eng::u8 MK, eng::u8 ME, class OnEvent>
	void advance(const Sequence<S, MT, MK, ME>& seq, eng::u16 ticks,
		     OnEvent&& on_event) noexcept {
		if (!playing || finished) {
			return;
		}
		const eng::u32 end = tick + ticks;
		if (seq.length == 0u) {
			tick = end;
			return;
		}
		if (end < static_cast<eng::u32>(seq.length)) {
			fire(seq, tick, end, on_event);
			tick = end;
			return;
		}
		fire(seq, tick, seq.length, on_event);
		if (!seq.loop) {
			tick = seq.length;
			finished = true;
			return;
		}
		const eng::u32 wrapped = (end - seq.length) % seq.length;
		fire(seq, 0u, wrapped, on_event);
		tick = wrapped;
	}

	/// Coloca el cursor en `t` (sin disparar eventos); reproduce desde ahí con `advance`.
	/// Útil para depurar/repetir. Si `t >= length` en una secuencia sin `loop`, queda
	/// `finished`.
	template <typename S, eng::u8 MT, eng::u8 MK, eng::u8 ME>
	void seek(const Sequence<S, MT, MK, ME>& seq, eng::u32 t) noexcept {
		tick = t;
		finished = !seq.loop && seq.length != 0u && t >= seq.length;
	}

private:
	/// Dispara los eventos con `tick` en `[from, to)` (una pasada por pista, orden de
	/// inserción dentro de cada pista).
	template <typename S, eng::u8 MT, eng::u8 MK, eng::u8 ME, class OnEvent>
	static void fire(const Sequence<S, MT, MK, ME>& seq, eng::u32 from, eng::u32 to,
			 OnEvent& on_event) noexcept {
		if (from >= to) {
			return;
		}
		for (eng::u8 tr = 0u; tr < MT; ++tr) {
			const EventTrack<ME>& track = seq.events[tr];
			for (eng::u8 i = 0u; i < track.count; ++i) {
				const eng::u32 ev = track.events[i].tick;
				if (ev >= from && ev < to) {
					on_event(track.events[i].id, ev);
				}
			}
		}
	}
};

} // namespace eng::util
