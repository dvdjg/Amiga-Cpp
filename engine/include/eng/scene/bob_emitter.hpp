#pragma once

/// \file bob_emitter.hpp
/// **Base común de emisión de BOBs** (`BobEmitter`): centraliza el destino, el
/// `FramePlan` y el contador de objetos dibujados para las capas de escena.
///
/// Las capas (`BobLayer`, `FastBobLayer`, y las capas de scroll que quieran BOBs)
/// no deben construir `BlitJob` ni gestionar el `BobTarget` por su cuenta: llaman
/// `begin(plan, target)`, dibujan con `draw(sheet, frame, x, y)` y consultan
/// `drawn()`. Así el destino/scroll y la contabilidad quedan en un único punto y
/// las políticas (fastcopy, cookie-cut, culling) solo deciden **qué** dibujar.
///
/// No posee memoria ni usa heap; las capacidades siguen en cada capa.

#include <eng/graphics/frame_plan.hpp>
#include <eng/graphics/sprite_asset.hpp>

namespace eng::scene {

/// Contexto de emisión de una capa de BOBs.
class BobEmitter {
public:
	/// Prepara la emisión: fija el plan y el destino. `fine_scroll` compensa el
	/// desplazamiento fino del playfield (el llamador puede aplicar el resto).
	void begin(eng::graphics::FramePlan& plan,
		   const eng::graphics::BobTarget& target) noexcept {
		m_plan = plan;
		m_target = target;
		m_drawn = 0u;
	}

	/// Reinicia la emisión sin cambiar de destino.
	void reset() noexcept { m_drawn = 0u; }

	/// Dibuja un frame de `sheet` en `(x,y)` (coordenadas de pantalla ya
	/// compensadas por el llamador). Devuelve `true` si el plan aceptó el blit.
	[[nodiscard]] bool draw(const eng::graphics::Sprite& sheet, eng::u8 frame,
				eng::s16 x, eng::s16 y) noexcept {
		// Un target de banda puede declarar `height == 0` (la altura la fija el
		// emisor); no usar `BobTarget::valid()` aquí porque lo rechazaría.
		if (!m_plan.valid() || m_target.planes.empty() || m_target.row_bytes == 0u ||
		    !sheet.valid()) {
			return false;
		}
		if (!sheet.draw(*m_plan, m_target, frame, x, y)) {
			return false;
		}
		++m_drawn;
		return true;
	}

	/// Número de BOBs aceptados por el plan en la emisión vigente.
	[[nodiscard]] constexpr eng::u16 drawn() const noexcept { return m_drawn; }
	/// Destino vigente (puede ser una banda con `height == 0`).
	[[nodiscard]] constexpr const eng::graphics::BobTarget& target() const noexcept {
		return m_target;
	}
	/// Plan vigente (vacío antes de `begin`).
	[[nodiscard]] constexpr eng::Ref<eng::graphics::FramePlan> plan() const noexcept {
		return m_plan;
	}

private:
	eng::Ref<eng::graphics::FramePlan> m_plan {};
	eng::graphics::BobTarget m_target {};
	eng::u16 m_drawn = 0u;
};

} // namespace eng::scene
