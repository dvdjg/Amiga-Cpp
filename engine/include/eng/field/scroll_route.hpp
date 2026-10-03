#pragma once

/// \file scroll_route.hpp
/// **Ruta de scroll continua** (`eng::playfield::ScrollRoute`) por fases: horizontal, vertical,
/// diagonal, circular y Lissajous. Avanza por **velocidad** (`x += vx; y += vy`), nunca por posición
/// absoluta por fase: así no hay **saltos** entre fases (los motores con `max_step` pequeño, como el
/// corcóscru, rechazan pasos > 1 px) y **cada frame mueve al menos un eje** (imagen distinta).
///
/// Cada eje se mueve como mucho **1 px/frame** (|vx|, |vy| <= 1). La X es periódica (el mundo
/// envuelve); la Y se acota a `[0, YMax]` con rebote. Compartida por las demos 204 (tiras) y 205
/// (XYLimited).

#include <eng/core/types/types.hpp>

namespace eng::playfield {

/// `YMax` = recorrido vertical útil; `PhaseFrames` = duración de cada fase.
template <eng::u16 YMax = 64u, eng::u16 PhaseFrames = 256u>
struct ScrollRoute {
	static constexpr eng::s32 y_max = YMax;   // widening u16 -> s32 (sin cast)
	static constexpr eng::s32 y_mid = YMax / 2;
	eng::s32 x = 1;
	eng::s32 y = y_mid;
	eng::s32 vx = 1;
	eng::s32 vy = 0;

	/// Avanza un frame de juego `f`. El llamador lee `x`/`y` (o usa el delta).
	void advance(eng::u32 f) noexcept {
		const eng::u32 phase = (f / PhaseFrames) % 5u;
		switch (phase) {
		case 0: // horizontal: x avanza, y se queda donde estaba (sin saltos)
			vx = 1;
			vy = 0;
			break;
		case 1: // vertical: x fijo, y rebota
			vx = 0;
			if (vy == 0) vy = 1;
			break;
		case 2: // diagonal: x avanza, y rebota
			vx = 1;
			if (vy == 0) vy = 1;
			break;
		case 3: { // circular: la VELOCIDAD rota por 8 direcciones (octógono), nunca (0,0)
			const eng::u32 k = ((f % PhaseFrames) * 8u) / PhaseFrames;
			static constexpr eng::s8 kcx[8] = {1, 1, 0, -1, -1, -1, 0, 1};
			static constexpr eng::s8 kcy[8] = {0, 1, 1, 1, 0, -1, -1, -1};
			vx = kcx[k & 7u];
			vy = kcy[k & 7u];
			break;
		}
		default: // Lissajous: x avanza, y oscila (velocidad alterna)
			vx = 1;
			vy = ((f / 40u) & 1u) != 0u ? 1 : -1;
			break;
		}
		// Rebote **antes** de mover: si la velocidad empujaría fuera del rango, se invierte; así el
		// frame nunca se queda clavado en el borde (siempre mueve >= 1 px en algún eje).
		if ((y >= y_max && vy > 0) || (y <= 0 && vy < 0)) vy = -vy;
		x += vx;
		y += vy;
		if (y < 0) {
			y = 0;
		} else if (y > y_max) {
			y = y_max;
		}
	}
};

} // namespace eng::playfield
