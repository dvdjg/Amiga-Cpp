#pragma once

/// \file plane_bobs.hpp
/// **Capa de BOBs de un plano** (`PlaneBobLayer`): actores que comparten una hoja
/// opaca con padding y eligen el plano del destino. El juego mueve actores; la capa
/// decide si el padding cubre la huella previa o si hay que borrarla, y puede marcar
/// cada actor como grupo atómico para el ejecutor beam-aware.
///
/// Es el camino de la demo 210: BOBs de un solo plano sobre un framebuffer
/// interleaved de 5 planos. La hoja debe ser planar de **1 plano**, `BobDraw::Opaque`
/// y con padding de color 0; el arte vive dentro de ese padding.
///
/// ```cpp
/// eng::scene::PlaneBobLayer bobs;
/// bobs.set_sheet(hoja, 2, 2);
/// bobs.resize(6);
/// bobs[0] = {x, y, frame, plane, true};
/// bobs.emit(plan, target);                 // sin guarda de haz
/// bobs.emit(plan, target, group_base);     // un grupo atómico por actor
/// ```
///
/// Contrato de movimiento: si un actor se desplaza más que `pad_x`/`pad_y` en un
/// frame, la capa emite un `ClearRect` de su huella previa antes de dibujar. Si no,
/// el padding de la copia opaca ya limpia la huella. Esto evita el residuo típico
/// de un BOB que avanza sobre fondo liso sin doble buffer.

#include <eng/graphics/bob.hpp>
#include <eng/graphics/frame_plan.hpp>

namespace eng::scene {

/// Actor de una capa de BOBs de un plano.
struct PlaneBobActor {
	eng::s16 x = 0;
	eng::s16 y = 0;
	eng::u8 frame = 0u;
	eng::u8 plane = 0u; ///< plano del destino que escribe este BOB
	bool visible = true;
};

/// Capa de BOBs de un plano con capacidad fija (sin heap).
class PlaneBobLayer {
public:
	static constexpr eng::u8 kMaxActors = 16u;

	/// Hoja compartida (un plano, opaca, con padding) y padding por lado.
	void set_sheet(const eng::graphics::Bob& sheet, eng::u8 pad_x, eng::u8 pad_y) noexcept {
		m_sheet = sheet;
		m_pad_x = pad_x;
		m_pad_y = pad_y;
	}
	void resize(eng::u8 n) noexcept { m_count = (n <= kMaxActors) ? n : kMaxActors; }
	[[nodiscard]] eng::u8 count() const noexcept { return m_count; }
	[[nodiscard]] eng::u8 pad_x() const noexcept { return m_pad_x; }
	[[nodiscard]] eng::u8 pad_y() const noexcept { return m_pad_y; }

	/// Acceso mutable al actor `i` (sin comprobar el rango; `i < count()`).
	[[nodiscard]] PlaneBobActor& operator[](eng::u8 i) noexcept { return m_actors[i]; }
	/// Acceso de solo lectura al actor `i`.
	[[nodiscard]] const PlaneBobActor& operator[](eng::u8 i) const noexcept {
		return m_actors[i];
	}

	/// Olvida las huellas previas tras un clear total del playfield de BOBs.
	void reset_history() noexcept {
		for (eng::u8 i = 0u; i < kMaxActors; ++i) {
			m_prev_x[i] = 0;
			m_prev_y[i] = 0;
			m_has[i] = false;
		}
	}

	/// Dibuja los actores visibles. `group_base != 0xFF` marca el erase+draw de cada
	/// actor como un grupo atómico (`group_base + i`) para el ejecutor beam-aware.
	/// Devuelve el número de actores dibujados.
	[[nodiscard]] eng::u16 emit(eng::graphics::FramePlan& plan,
				    const eng::graphics::BobTarget& target,
				    eng::u8 group_base = 0xFFu) noexcept {
		eng::u16 drawn = 0u;
		for (eng::u8 i = 0u; i < m_count; ++i) {
			const PlaneBobActor& a = m_actors[i];
			if (!a.visible) {
				if (m_has[i]) {
					(void)eng::graphics::bob_erase_plane_box(
						plan, m_sheet, m_sheet.width, m_sheet.height, m_prev_x[i],
						m_prev_y[i], target, a.plane);
					m_has[i] = false;
				}
				continue;
			}
			const eng::u8 first = plan.blit_job_count();
			const bool moved = m_has[i] && (diff(a.x, m_prev_x[i]) > m_pad_x ||
							diff(a.y, m_prev_y[i]) > m_pad_y);
			if (moved) {
				(void)eng::graphics::bob_erase_plane_box(
					plan, m_sheet, m_sheet.width, m_sheet.height, m_prev_x[i],
					m_prev_y[i], target, a.plane);
			}
			if (!eng::graphics::bob_draw_plane_opaque(plan, m_sheet, a.frame, a.x, a.y,
								  target, a.plane)) {
				continue;
			}
			if (group_base != 0xFFu) {
				plan.tag_bob_group(static_cast<eng::u8>(group_base + i), first);
			}
			m_prev_x[i] = a.x;
			m_prev_y[i] = a.y;
			m_has[i] = true;
			++drawn;
		}
		return drawn;
	}

private:
	/// Distancia absoluta entre dos coordenadas (para comparar con el padding).
	[[nodiscard]] static eng::u16 diff(eng::s16 a, eng::s16 b) noexcept {
		return static_cast<eng::u16>(a > b ? a - b : b - a);
	}

	eng::graphics::Bob m_sheet {};
	PlaneBobActor m_actors[kMaxActors] {};
	eng::s16 m_prev_x[kMaxActors] {};
	eng::s16 m_prev_y[kMaxActors] {};
	bool m_has[kMaxActors] {};
	eng::u8 m_count = 0u;
	eng::u8 m_pad_x = 0u;
	eng::u8 m_pad_y = 0u;
};

} // namespace eng::scene
