#pragma once

#include <eng/graphics/blit_job.hpp>
#include <eng/graphics/playfield_scroll.hpp>
#include <eng/graphics/composition/compose.hpp>
#include <eng/graphics/composition/copper_chunky.hpp>
#include <eng/graphics/effects/raster_gradient.hpp>
#include <eng/graphics/effects/rotozoom.hpp>
#include <eng/graphics/sprite_channel_window.hpp>
#include <eng/core/types/memory_kind.hpp>

namespace eng::effects {

/// **Rotozoom de alto nivel**: estado (ángulo/zoom/offset) + render de la textura indexada
/// a un `ChunkyBuffer` (para C2P). Envuelve `graphics::rotozoom_into`
/// (`graphics/effects/rotozoom.hpp`); es un `RenderEffect` (escribe píxeles, no Copper).
class Rotozoom {
public:
	/// Fija los parámetros (punto fijo 16.16; `angle` en fases 0..255).
	void configure(graphics::Rotozoom r) noexcept { m_r = r; }
	/// Anima la rotación (fase 0..255).
	void set_angle(eng::u16 phase) noexcept { m_r.angle = phase; }
	[[nodiscard]] const graphics::Rotozoom& params() const noexcept { return m_r; }

	/// Rota/escala la textura `TW×TH` a `dst` (`w×h` índices; `w` múltiplo de 16).
	template <eng::u16 TW, eng::u16 TH>
	void render(eng::IndexedTexture tex, eng::ChunkyBuffer dst, eng::u16 w, eng::u16 h) const {
		graphics::rotozoom_into<TW, TH>(tex, m_r, dst, w, h);
	}

private:
	graphics::Rotozoom m_r {};
};

} // namespace eng::effects
