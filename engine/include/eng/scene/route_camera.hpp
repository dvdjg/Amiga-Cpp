#pragma once

/// \file route_camera.hpp
/// Camara de ruta por fases para demos de scroll infinitas.
///
/// Recorre fases de movimiento, cada una precedida de un breve paron: horizontal,
/// vertical, diagonal, circular y senoidal. Las fases lineales se recorren a
/// 1 px/frame para que la animacion sea suave (el chipset desplaza 1 px minimo
/// por frame con BPLCON1); circulo y seno duran lo suficiente para que el angulo
/// no salte entre pasos de la tabla. Despues de `jump_start_frames` entra en
/// modo de saltos: cada frame avanza un paso aleatorio de 2..15 px en las
/// direcciones horizontal/vertical, y cada `repattern_frames` se vuelven a
/// elegir al azar las direcciones.

#include <eng/core/types.hpp>

namespace eng::scene {

/// Camara de ruta por fases.
struct RouteCamera {
	/// Limites de la camara dentro de la superficie.
	eng::u16 min_x = 1;
	eng::u16 max_x = 320;
	eng::u16 min_y = 0;
	eng::u16 max_y = 256;
	/// Centro del circulo y del seno, y radio (escala de los offsets x64).
	eng::u16 center_x = 160;
	eng::u16 center_y = 128;
	eng::u16 radius_scale = 96;
	/// Paron (frames) al inicio de cada fase.
	eng::u16 pause_frames = 10;
	/// A partir de este frame se activa el modo de saltos.
	eng::u32 jump_start_frames = 1650;
	/// Cada cuantos frames se vuelven a elegir las direcciones de salto.
	eng::u32 repattern_frames = 500;

	/// Espeja la posicion horizontal (parallax opuesto).
	bool mirror_x = false;

	/// Estado actual de la camara.
	eng::u16 x = 1;
	eng::u16 y = 128;
	eng::s8 h_dir = 1;
	eng::s8 v_dir = 1;
	eng::u32 jump_epoch = 0xffffffffu;
	eng::u32 rng = 0x12345678u;

	/// Duracion (frames) del movimiento de cada fase.
	///
	/// Las fases lineales (horizontal, vertical, diagonal) se recorren a
	/// 1 px/frame (suave): el Amiga solo sabe desplazar 1 px minimo por frame con
	/// el fine scroll (BPLCON1), asi que cualquier paso mayor se ve a trompicones.
	/// Circulo y seno duran mas frames para que el angulo avance < 1 paso por
	/// frame (si no, el salto entre pasos de la tabla circular seria de varios px).
	static constexpr eng::u32 phase_move_dur[5] {192, 192, 192, 512, 512};

	/// Avanza la camara un frame.
	void advance(eng::u32 frame_index) {
		if (frame_index >= jump_start_frames) {
			advance_jump(frame_index);
			return;
		}
		eng::u32 t = frame_index;
		for (eng::u8 p = 0; p < 5; ++p) {
			if (t < pause_frames) {
				return; // paron: la camara se queda quieta
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
	/// Tabla de offsets X de una circunferencia de 64 pasos y radio 64.
	static constexpr eng::s16 circle_offset_x(eng::u8 index) {
		constexpr eng::s16 offsets[] {
			64, 64, 63, 61, 59, 56, 53, 49,
			45, 41, 36, 31, 24, 18, 12, 6,
			0, -6, -12, -18, -24, -31, -36, -41,
			-45, -49, -53, -56, -59, -61, -63, -64,
			-64, -64, -63, -61, -59, -56, -53, -49,
			-45, -41, -36, -31, -24, -18, -12, -6,
			0, 6, 12, 18, 24, 31, 36, 41,
			45, 49, 53, 56, 59, 61, 63, 64,
		};
		return offsets[index & 63u];
	}

	/// Tabla de offsets Y (seno) de la misma circunferencia.
	static constexpr eng::s16 circle_offset_y(eng::u8 index) {
		constexpr eng::s16 offsets[] {
			0, 6, 12, 18, 24, 31, 36, 41,
			45, 49, 53, 56, 59, 61, 63, 64,
			64, 64, 63, 61, 59, 56, 53, 49,
			45, 41, 36, 31, 24, 18, 12, 6,
			0, -6, -12, -18, -24, -31, -36, -41,
			-45, -49, -53, -56, -59, -61, -63, -64,
			-64, -64, -63, -61, -59, -56, -53, -49,
			-45, -41, -36, -31, -24, -18, -12, -6,
		};
		return offsets[index & 63u];
	}

	/// Escala un offset x64 al radio configurado.
	constexpr eng::s16 radius_signed(eng::s16 offset) const {
		return static_cast<eng::s16>(radius_scale * offset / 64);
	}

	void apply_phase(eng::u8 p, eng::u32 t) {
		eng::u16 route_x = x;
		eng::u16 route_y = y;
		switch (p) {
		case 0: { // horizontal derecha, 1 px/frame (suave)
			route_x = static_cast<eng::u16>(min_x + t);
			route_y = center_y;
			break;
		}
		case 1: { // vertical abajo, 1 px/frame (suave)
			route_x = center_x;
			route_y = static_cast<eng::u16>(min_y + t);
			break;
		}
		case 2: { // diagonal, 1 px/frame en cada eje (suave)
			route_x = static_cast<eng::u16>(min_x + t);
			route_y = static_cast<eng::u16>(min_y + t);
			break;
		}
		case 3: { // circular: el angulo avanza < 1 paso por frame (tabla de 64)
			const eng::u8 a = static_cast<eng::u8>((t * 64u) / phase_move_dur[3]);
			route_x = static_cast<eng::u16>(center_x + radius_signed(circle_offset_x(a)));
			route_y = static_cast<eng::u16>(center_y + radius_signed(circle_offset_y(a)));
			break;
		}
		case 4: { // senoidal: x avanza 1 px/frame, y oscila suave (un ciclo)
			const eng::u8 a = static_cast<eng::u8>((t * 64u) / phase_move_dur[4]);
			route_x = static_cast<eng::u16>(min_x + t);
			route_y = static_cast<eng::u16>(center_y + radius_signed(circle_offset_y(a)));
			break;
		}
		}
		store(route_x, route_y);
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
		eng::s32 nx = static_cast<eng::s32>(x) + dx;
		eng::s32 ny = static_cast<eng::s32>(y) + dy;
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
		store(static_cast<eng::u16>(nx), static_cast<eng::u16>(ny));
	}

	/// Aplica el espejo horizontal y guarda la posicion.
	void store(eng::u16 route_x, eng::u16 route_y) {
		x = mirror_x
			? static_cast<eng::u16>(static_cast<eng::u32>(max_x) + min_x - route_x)
			: route_x;
		y = route_y;
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
