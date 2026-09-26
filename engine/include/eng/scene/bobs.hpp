#pragma once

/// \file bobs.hpp
/// **Capa de BOBs** (`eng::scene::BobLayer`): una hoja de sprites (`graphics::Sprite`) y `N`
/// **actores** (`Actor`: posición, frame, visibilidad). El juego mueve actores por frame; `emit`
/// dibuja los visibles al `FramePlan` sin que vea `BlitJob`, minterns ni strides.
///
/// ```cpp
/// eng::scene::BobLayer bobs {};
/// bobs.set_sheet(nave);                     // Sprite del asset
/// bobs.resize(16);
/// for (u8 i = 0; i < 16; ++i) bobs[i] = { x, y, frame, true };
/// bobs.emit(plan, scene.bob_target());      // una pasada por actor visible
/// ```

#include <eng/graphics/frame_plan.hpp>
#include <eng/graphics/raster_intent.hpp>
#include <eng/graphics/sprite_asset.hpp>

namespace eng::scene {

/// **Actor** de una capa de BOBs: dónde se dibuja y qué frame muestra.
struct Actor {
	eng::s16 x = 0;
	eng::s16 y = 0;
	eng::u8 frame = 0u;
	bool visible = true;
};

/// Capa de BOBs de capacidad fija (sin heap): una hoja + actores.
class BobLayer {
public:
	static constexpr eng::u8 kMaxActors = 32u;

	/// Asocia la hoja de sprites (el `Sprite` del asset). Sin hoja válida, `emit` no dibuja.
	void set_sheet(eng::graphics::Sprite sheet) noexcept { m_sheet = sheet; }
	[[nodiscard]] const eng::graphics::Sprite& sheet() const noexcept { return m_sheet; }

	/// Fija el número de actores en uso (`<= kMaxActors`).
	void resize(eng::u8 n) noexcept { m_count = (n <= kMaxActors) ? n : kMaxActors; }
	[[nodiscard]] eng::u8 count() const noexcept { return m_count; }

	[[nodiscard]] Actor& operator[](eng::u8 i) noexcept { return m_actors[i]; }
	[[nodiscard]] const Actor& operator[](eng::u8 i) const noexcept { return m_actors[i]; }

	/// Dibuja los actores **visibles** en `target`; devuelve cuántos se dibujaron.
	[[nodiscard]] eng::u16 emit(eng::graphics::FramePlan& plan,
				    const eng::graphics::BobTarget& target) const {
		eng::u16 drawn = 0u;
		for (eng::u8 i = 0u; i < m_count; ++i) {
			const Actor& a = m_actors[i];
			if (a.visible && m_sheet.draw(plan, target, a.frame, a.x, a.y)) {
				++drawn;
			}
		}
		return drawn;
	}

private:
	eng::graphics::Sprite m_sheet {};
	Actor m_actors[kMaxActors] {};
	eng::u8 m_count = 0u;
};

} // namespace eng::scene
