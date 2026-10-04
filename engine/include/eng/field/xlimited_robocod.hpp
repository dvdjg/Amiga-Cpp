#pragma once

/// \file xlimited_robocod.hpp
/// **Copia del fondo RoboCod** de un `XlimitedScene` (etapa 5 del planner, `ROADMAP_GAME_API.md`
/// §7): repinta la **ventana visible del patrón de fondo** al plano de *parallax* del campo
/// (`cfg.parallax_plane`), en 1 o 2 tramos para compensar el **split del corcóscru**, **espera al
/// blanking** (para que el haz no lea una fila a medio reescribir) y **conmuta el doble buffer** del
/// fondo. Reúne el cableado que antes hacía cada demo RoboCod (112): `bg_window_for` +
/// `bg_split_rects` + `make_bg_plane_copy_rect_job` + `bg_flip`.
///
/// El juego solo aporta el **patrón** (con su ancho de fila y período) y su propia cámara de fondo
/// (`scroll`, que avanza `bg_dx`/frame). Llámalo cada frame **después** del scroll del campo y
/// **antes** de `compose()`, en el *blanking* vertical (`blank_line`).

#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/field/soft_dpf.hpp>
#include <eng/graphics/frame_plan.hpp>

namespace eng::playfield {

/// Copia el fondo RoboCod de `scene` al `plan`: ventana + split + *blanking* + `bg_flip`. `false` si
/// no hay parallax/patrón o un blit no cupo. `scroll` es la cámara propia del fondo (se avanza).
/// \param scene              escena (config + `bg()`).
/// \param backend            el backend.
/// \param pattern            patrón de fondo.
/// \param pattern_row_bytes  bytes por fila del patrón.
/// \param period_px          período horizontal del patrón en px.
/// \param scroll             cámara propia del fondo (se avanza `bg_dx`).
/// \param bg_dx              avance del fondo este frame.
/// \param blank_line         línea de blanking del raster.
/// \param plan               plan del frame.
/// \return `false` si no hay parallax/patrón o un blit no cupo.
template <class SceneT, class Backend>
[[nodiscard]] bool robocod_bg_frame(SceneT& scene, Backend& backend, eng::Pattern pattern,
				    eng::u16 pattern_row_bytes, eng::u16 period_px, eng::s32& scroll,
				    eng::s32 bg_dx, eng::u16 blank_line,
				    eng::graphics::FramePlan& plan) noexcept {
	const auto& cfg = scene.config();
	if (cfg.parallax_plane == 0xffu || pattern.empty() || period_px == 0u) {
		return false;
	}
	// Ventana horizontal (guarda + visible) y desplazamiento por la cámara propia del fondo.
	BgWindow win = bg_window_for(scene.bg().videoposx(), period_px,
				     static_cast<eng::u16>(cfg.viewport_w / 8u + 2u));
	scroll += bg_dx;
	if (scroll >= static_cast<eng::s32>(period_px)) {
		scroll = 0;
	}
	win.src_x = static_cast<eng::u16>(
		(static_cast<eng::s32>(win.src_x) + scroll) % static_cast<eng::s32>(period_px));

	const BgSplitRects rects = bg_split_rects(scene.bg().display_offset(), cfg.display_height,
						  cfg.viewport_h, 0u);
	plan.clear();
	plan.set_blit_budget_limits({8192, 16384, 4u, 200u});
	for (eng::u8 i = 0u; i < rects.count; ++i) {
		if (!plan.add_tile_block_copy(scene.bg().make_bg_plane_copy_rect_job(
			pattern, pattern_row_bytes, win.src_x, rects.src_y[i], rects.dest_row[i],
			rects.rows[i], win.dest_byte_off, win.words))) {
			return false;
		}
	}
	// Espera al inicio del blanking vertical (si el backend lo expone).
	if (blank_line != 0u) {
		if constexpr (requires(Backend& b) { b.current_raster_line(); }) {
			for (;;) {
				if (backend.current_raster_line() == blank_line) {
					break;
				}
			}
		}
	}
	if (!backend.execute_frame_plan(plan)) {
		return false;
	}
	scene.bg().bg_flip(); // el blit fue al buffer trasero; pasa a delantero
	return true;
}

} // namespace eng::playfield
