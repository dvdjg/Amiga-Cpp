#pragma once

/// \file route_camera.hpp
/// Cámara de ruta por fases para demos de scroll infinitas.
///
/// Recorre fases de movimiento, cada una precedida de un breve parón: horizontal,
/// vertical, diagonal, circular y senoidal. Las fases lineales se recorren a 1 px/frame
/// para que la animación sea suave (el chipset desplaza 1 px mínimo por frame con
/// BPLCON1); círculo y seno duran lo suficiente para que el ángulo no salte entre pasos
/// de la tabla. Después de `jump_start_frames` entra en modo de saltos: cada frame avanza
/// un paso aleatorio de 2..15 px en horizontal/vertical y cada `repattern_frames` se
/// vuelven a elegir las direcciones.
///
/// La posición es un `Vec<2, Coord>` (LONGITUD, píxeles) y los offsets de la
/// circunferencia se generan con **`eng::SineTable`** (el generador de seno en
/// compile-time del engine), no con una tabla escrita a mano: una sola fuente de seno
/// para todo el engine. La tabla es la 4.12 (`SineTable<4096,64>`) y el offset de
/// radio 64 se obtiene redondeando al entero más cercano.

#include <eng/core/math/linalg.hpp>
#include <eng/core/math/scalar.hpp>
#include <eng/core/math/sinetable.hpp>
#include <eng/core/types/types.hpp>

namespace eng::scene {

/// Coordenada de pantalla/mundo en LONGITUD (píxeles): el escalar de coordenada central.
using Coord = eng::coord;

/// Crudo (entero s16) de una coordenada, genérico sobre el escalar.
[[nodiscard]] constexpr eng::s16 coord_raw(Coord c) {
	return static_cast<eng::s16>(eng::math::scalar_traits<Coord>::to_int(c));
}

/// Cámara de ruta por fases.
struct RouteCamera {
	/// Límites de la cámara dentro de la superficie.
	eng::u16 min_x = 1;
	eng::u16 max_x = 320;
	eng::u16 min_y = 0;
	eng::u16 max_y = 256;
	/// Centro del círculo y del seno, y radio (escala de los offsets x64).
	eng::u16 center_x = 160;
	eng::u16 center_y = 128;
	eng::u16 radius_scale = 96;
	/// Parón (frames) al inicio de cada fase. 0 = movimiento continuo sin pausas
	/// (transiciones suaves entre fases).
	eng::u16 pause_frames = 0;
	/// A partir de este frame se activa el modo de saltos.
	eng::u32 jump_start_frames = 1600;
	/// Cada cuántos frames se vuelven a elegir las direcciones de salto.
	eng::u32 repattern_frames = 500;

	/// Espeja la posición horizontal (parallax opuesto).
	bool mirror_x = false;

	/// Posición actual de la cámara (LONGITUD, píxeles).
	eng::math::Vec<2, Coord> pos {{Coord {1}, Coord {128}}};
	eng::s8 h_dir = 1;
	eng::s8 v_dir = 1;
	eng::u32 jump_epoch = 0xffffffffu;
	eng::u32 rng = 0x12345678u;

	/// Coordenadas de la cámara para el consumidor (píxeles).
	[[nodiscard]] eng::u16 x() const { return static_cast<eng::u16>(coord_raw(pos.v[0])); }
	[[nodiscard]] eng::u16 y() const { return static_cast<eng::u16>(coord_raw(pos.v[1])); }

	/// Fija la posición (píxeles) sin espejo.
	void set(eng::u16 nx, eng::u16 ny) {
		pos = {{Coord {static_cast<eng::s16>(nx)}, Coord {static_cast<eng::s16>(ny)}}};
	}

	/// Centro del círculo/seno como vector tipado.
	[[nodiscard]] eng::math::Vec<2, Coord> center() const {
		return {{Coord {static_cast<eng::s16>(center_x)},
			 Coord {static_cast<eng::s16>(center_y)}}};
	}

	/// Duración (frames) del movimiento de cada fase.
	///
	/// Las fases lineales (horizontal, vertical, diagonal) se recorren a 1 px/frame
	/// (suave): el Amiga solo sabe desplazar 1 px mínimo por frame con el fine scroll
	/// (BPLCON1), así que cualquier paso mayor se ve a trompicones. Círculo y seno duran
	/// más frames para que el ángulo avance < 1 paso por frame.
	static constexpr eng::u32 phase_move_dur[5] {192, 192, 192, 512, 512};

	/// Avanza la cámara un frame.
	void advance(eng::u32 frame_index) {
		if (frame_index >= jump_start_frames) {
			advance_jump(frame_index);
			return;
		}
		eng::u32 t = frame_index;
		for (eng::u8 p = 0; p < 5; ++p) {
			if (t < pause_frames) {
				return; // parón: la cámara se queda quieta
			}
			t -= pause_frames;
			if (t < phase_move_dur[p]) {
				apply_phase(p, t);
				return;
			}
			t -= phase_move_dur[p];
		}
	}

private:
	/// Tabla 4.12 de 64 muestras (una vuelta). `operator[]` indexa en runtime sin float.
	inline static constexpr eng::SineTable<4096, 64> kCircle {};

	/// Muestra 4.12 -> entero redondeado al más cercano (el offset de radio 64).
	static constexpr eng::s16 round64(eng::s32 sample12) {
		return static_cast<eng::s16>(
			sample12 >= 0 ? ((sample12 + 32) >> 6) : -(((-sample12) + 32) >> 6));
	}
	/// Offset X de la circunferencia (coseno) y Y (seno), radio 64.
	static constexpr eng::s16 cos64(eng::u8 index) {
		return round64(kCircle[(index + 16u) & 63u]);
	}
	static constexpr eng::s16 sin64(eng::u8 index) { return round64(kCircle[index & 63u]); }

	/// Escala un offset de radio 64 al radio configurado.
	constexpr eng::s16 radius_signed(eng::s16 offset) const {
		return static_cast<eng::s16>(radius_scale * offset / 64);
	}

	/// Offset de la circunferencia (radio configurado) como vector tipado.
	[[nodiscard]] eng::math::Vec<2, Coord> circle_offset(eng::u8 a) const {
		return {{Coord {radius_signed(cos64(a))}, Coord {radius_signed(sin64(a))}}};
	}

	void apply_phase(eng::u8 p, eng::u32 t) {
		eng::math::Vec<2, Coord> route = pos;
		switch (p) {
		case 0: // horizontal derecha, 1 px/frame (suave)
			route.v[0] = Coord {static_cast<eng::s16>(min_x + t)};
			route.v[1] = Coord {static_cast<eng::s16>(center_y)};
			break;
		case 1: // vertical abajo, 1 px/frame (suave)
			route.v[0] = Coord {static_cast<eng::s16>(center_x)};
			route.v[1] = Coord {static_cast<eng::s16>(min_y + t)};
			break;
		case 2: // diagonal, 1 px/frame en cada eje (suave)
			route.v[0] = Coord {static_cast<eng::s16>(min_x + t)};
			route.v[1] = Coord {static_cast<eng::s16>(min_y + t)};
			break;
		case 3: { // circular: el ángulo avanza < 1 paso por frame (tabla de 64)
			const eng::u8 a = static_cast<eng::u8>((t * 64u) / phase_move_dur[3]);
			route = center() + circle_offset(a);
			break;
		}
		case 4: { // senoidal: x avanza 1 px/frame, y oscila suave (un ciclo)
			const eng::u8 a = static_cast<eng::u8>((t * 64u) / phase_move_dur[4]);
			route.v[0] = Coord {static_cast<eng::s16>(min_x + t)};
			route.v[1] = Coord {
				static_cast<eng::s16>(center_y + radius_signed(sin64(a)))};
			break;
		}
		}
		store(route);
	}

	void advance_jump(eng::u32 frame_index) {
		const eng::u32 epoch = (frame_index - jump_start_frames) / repattern_frames;
		if (epoch != jump_epoch) {
			jump_epoch = epoch;
			h_dir = (rng_next() & 1u) != 0u ? 1 : -1;
			v_dir = (rng_next() & 1u) != 0u ? 1 : -1;
		}
		const eng::s32 step = 2 + static_cast<eng::s32>(rng_next() % 14u);
		const eng::s32 dx = step * h_dir;
		const eng::s32 dy = step * v_dir;
		eng::s32 nx = static_cast<eng::s32>(x()) + dx;
		eng::s32 ny = static_cast<eng::s32>(y()) + dy;
		if (nx < static_cast<eng::s32>(min_x)) {
			nx = min_x;
			h_dir = 1;
		} else if (nx > static_cast<eng::s32>(max_x)) {
			nx = max_x;
			h_dir = -1;
		}
		if (ny < static_cast<eng::s32>(min_y)) {
			ny = min_y;
			v_dir = 1;
		} else if (ny > static_cast<eng::s32>(max_y)) {
			ny = max_y;
			v_dir = -1;
		}
		store({{Coord {static_cast<eng::s16>(nx)}, Coord {static_cast<eng::s16>(ny)}}});
	}

	/// Aplica el espejo horizontal y guarda la posición.
	void store(eng::math::Vec<2, Coord> route) {
		const eng::s16 rx = coord_raw(route.v[0]);
		route.v[0] = Coord {mirror_x
			? static_cast<eng::s16>(static_cast<eng::u32>(max_x) + min_x - static_cast<eng::u32>(static_cast<eng::u16>(rx)))
			: rx};
		pos = route;
	}

	eng::u32 rng_next() {
		eng::u32 z = rng;
		z ^= z << 13u;
		z ^= z >> 17u;
		z ^= z << 5u;
		rng = z;
		return z;
	}
};

} // namespace eng::scene
