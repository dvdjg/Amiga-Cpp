#pragma once

/// \file pan_window.hpp
/// **Paneo de la ventana de display** para fotogramas intermedios (*fake frames*): política
/// pura, sin hardware, que decide cuándo toca renderizar un fotograma **completo** (Full) y
/// cuándo presentar el anterior **paneado** (Fake) sobre un framebuffer mayor que el viewport.
///
/// Modelo (ver `docs/reference/amiga/techniques/camera-pan-fake-frames.md`):
///
/// - El framebuffer de la escena lleva **márgenes** (`margin_x`/`margin_y`) alrededor del
///   viewport; la pasada completa renderiza viewport + márgenes.
/// - El llamador acumula el **paneo de cámara en píxeles de ventana** (`pan`, entero): cuánto
///   debería haberse desplazado la imagen desde el origen del framebuffer. La calibración
///   ángulo→píxeles (focal) es del llamador; aquí solo se decide Full o Fake y el offset.
/// - **Full**: se renderiza la escena con la cámara actual **en el origen** (offset 0) y se
///   ancla `pan` como base.
/// - **Fake**: se muestra el framebuffer anterior desplazando la ventana `pan - base` px,
///   siempre que el offset quepa en los márgenes y no se supere la racha de fakes.
///
/// La monotonía del paneo respecto a la cámara (signo) la fija el llamador: aquí el offset es
/// *px de ventana dentro del framebuffer*, positivo hacia la derecha/abajo del framebuffer.

#include <eng/core/types/types.hpp>

namespace eng::graphics {

/// Política del paneo de presentación.
struct PanWindowPolicy {
	eng::s16 margin_x = 8;           ///< margen horizontal del framebuffer a cada lado (px)
	eng::s16 margin_y = 8;           ///< margen vertical arriba/abajo (px)
	eng::u8 max_consecutive_fakes = 2; ///< tras N fakes seguidos se fuerza un Full
};

/// Estado entre frames (una instancia por controlador de presentación).
struct PanWindowState {
	eng::s32 base_x = 0;   ///< paneo (px) con el que se renderizó el último Full
	eng::s32 base_y = 0;   ///< idem vertical
	eng::u8 fakes = 0;     ///< fakes consecutivos desde el último Full
	bool anchored = false; ///< `false` = aún no hay Full de referencia (o se pidió uno)
};

/// Resultado de un paso de presentación.
struct PanWindowStep {
	bool full = true;    ///< `true` = toca pasada completa; `false` = presentar paneado
	eng::s16 offset_x = 0; ///< px de desplazamiento de la ventana dentro del framebuffer
	eng::s16 offset_y = 0;
};

/// Decide el modo del frame actual. Actualiza `state` (base/racha) en los Full.
/// \param state  estado entre frames (se modifica).
/// \param pan_x,pan_y  paneo acumulado de la cámara en px de ventana.
/// \param policy  márgenes y racha máxima.
/// \return el paso: Full (offset 0, base re-anclada) o Fake (offset exacto a aplicar).
[[nodiscard]] constexpr PanWindowStep pan_window_step(PanWindowState& state, eng::s32 pan_x,
						     eng::s32 pan_y,
						     const PanWindowPolicy& policy) noexcept {
	const eng::s32 dx = pan_x - state.base_x;
	const eng::s32 dy = pan_y - state.base_y;
	const eng::s32 adx = (dx < 0) ? -dx : dx;
	const eng::s32 ady = (dy < 0) ? -dy : dy;
	if (!state.anchored || adx > policy.margin_x || ady > policy.margin_y ||
	    state.fakes >= policy.max_consecutive_fakes) {
		state.base_x = pan_x;
		state.base_y = pan_y;
		state.fakes = 0u;
		state.anchored = true;
		return PanWindowStep {true, 0, 0};
	}
	++state.fakes;
	return PanWindowStep {false, static_cast<eng::s16>(dx), static_cast<eng::s16>(dy)};
}

/// Fuerza que el próximo paso sea Full (cambio de escena, teletransporte, cambio de nivel).
constexpr void pan_window_force_full(PanWindowState& state) noexcept {
	state.anchored = false;
	state.fakes = 0u;
}

/// `true` si el paso cabe en los márgenes (invariante de los Fake).
[[nodiscard]] constexpr bool pan_window_offset_fits(const PanWindowStep& step,
						    const PanWindowPolicy& policy) noexcept {
	return step.offset_x >= -policy.margin_x && step.offset_x <= policy.margin_x &&
	       step.offset_y >= -policy.margin_y && step.offset_y <= policy.margin_y;
}

} // namespace eng::graphics
