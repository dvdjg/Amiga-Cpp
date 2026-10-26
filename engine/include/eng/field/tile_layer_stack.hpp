#pragma once

#include <eng/field/tile_animation.hpp>
#include <eng/graphics/frame_plan.hpp>

namespace eng::playfield {

enum class TileLayerMode : eng::u8 {
	Baked = 0,
	Base = 1,
	Overlay = 2,
	AnimatedOverlay = 3,
	Occluder = 4,
	Parallax = 5,
	Skip = 6,
};

[[nodiscard]] constexpr bool tile_layer_is_live(TileLayerMode mode) noexcept {
	return mode == TileLayerMode::Overlay || mode == TileLayerMode::AnimatedOverlay ||
	       mode == TileLayerMode::Occluder;
}

[[nodiscard]] constexpr bool tile_layer_is_occluder(TileLayerMode mode) noexcept {
	return mode == TileLayerMode::Occluder;
}

/// Una capa viva en la pila. `anim_layer` es el indice de `TileAnimator::layer`
/// (el cooker conserva el orden de capas); `opaque` permite usar copia directa
/// cuando el arte no tiene transparencia.
enum class TileAnimationBudgetPolicy : eng::u8 {
	Off = 0,
	Preserve50 = 1,
	TargetFps = 2,
};

enum class TileAnimationPriority : eng::u8 {
	Background = 0,
	Decor = 64,
	Foreground = 128,
};

struct TileLayerBinding {
	eng::u8 anim_layer = 0u;
	TileLayerMode mode = TileLayerMode::Baked;
	bool opaque = false;
	eng::u8 priority = static_cast<eng::u8>(TileAnimationPriority::Decor);
};

/// Pila de capas de tiles para el campo X-Limited.
///
/// La base cocida se dibuja fuera de este objeto (rapida, opaca). La pila solo
/// compone las capas vivas que el cooker dejo fuera de la base, en su orden
/// declarado. `Occluder` no se pinta en el pase normal: se pinta despues de los
/// actores y solo si su celda intersecta un rectangulo sucio.
class TileLayerStack {
public:
	constexpr TileLayerStack() noexcept = default;

	constexpr void configure(eng::Span<const TileLayerBinding> layers,
				 eng::u8 baked_through = 0xffu) noexcept {
		m_layers = layers;
		m_baked_through = baked_through;
	}

	[[nodiscard]] constexpr bool configured() const noexcept {
		return !m_layers.empty();
	}
	[[nodiscard]] constexpr eng::u8 baked_through() const noexcept {
		return m_baked_through;
	}
	[[nodiscard]] constexpr bool is_live(eng::u8 anim_layer) const noexcept {

		for (eng::u16 i = 0u; i < m_layers.size(); ++i) {

			const TileLayerBinding& b = m_layers[i];

			if (b.anim_layer == anim_layer) return tile_layer_is_live(b.mode);

		}

		return false;

	}



	[[nodiscard]] constexpr bool has_occluders() const noexcept {

		for (eng::u16 i = 0u; i < m_layers.size(); ++i) {

			if (m_layers[i].mode == TileLayerMode::Occluder) return true;

		}

		return false;

	}



	[[nodiscard]] constexpr eng::u16 live_count() const noexcept {
		eng::u16 n = 0u;
		for (eng::u16 i = 0u; i < m_layers.size(); ++i) {
			if (tile_layer_is_live(m_layers[i].mode)) ++n;
		}
		return n;
	}

	/// Compone las capas `Overlay`/`AnimatedOverlay` de una celda visible.
	/// Devuelve false solo si el plan o el animador rechazan un trabajo.
	[[nodiscard]] bool draw_block(TileAnimator& anim, graphics::FramePlan& plan,
				      const graphics::BobTarget& target, eng::u16 ring_height,
				      eng::u16 x, eng::u16 ring_y, eng::u16 mapx,
				      eng::u16 mapy) const noexcept {
		for (eng::u16 i = 0u; i < m_layers.size(); ++i) {
			const TileLayerBinding& b = m_layers[i];
			if (b.mode != TileLayerMode::Overlay &&
			    b.mode != TileLayerMode::AnimatedOverlay) {
				continue;
			}
			if (!anim.draw_layer_block(plan, target, ring_height, x, ring_y,
						   mapx, mapy, b.anim_layer, b.opaque)) {
				return false;
			}
		}
		return true;
	}

	[[nodiscard]] static constexpr bool cell_intersects(eng::u16 x, eng::u16 y,
							  const graphics::DirtyRect& r) noexcept {
		return r.valid() && static_cast<eng::s32>(x) + 16 > r.left &&
		       static_cast<eng::s32>(y) + 16 > r.top &&
		       static_cast<eng::s32>(x) < r.right &&
		       static_cast<eng::s32>(y) < r.bottom;
	}

	/// Redibuja solo celdas de capas `Occluder` que intersectan algun dirty rect.
	[[nodiscard]] bool draw_occluders(const TileAnimator& anim, graphics::FramePlan& plan,
					  const graphics::BobTarget& target,
					  eng::u16 ring_height, eng::u16 x, eng::u16 ring_y,
					  eng::u16 mapx, eng::u16 mapy,
					  eng::Span<const graphics::DirtyRect> dirty) const noexcept {
		bool intersects = false;
		for (eng::u16 i = 0u; i < dirty.size(); ++i) {
			if (cell_intersects(x, ring_y, dirty[i])) {
				intersects = true;
				break;
			}
		}
		if (!intersects) return true;
		for (eng::u16 i = 0u; i < m_layers.size(); ++i) {
			const TileLayerBinding& b = m_layers[i];
			if (b.mode != TileLayerMode::Occluder) continue;
			if (!anim.draw_layer_block(plan, target, ring_height, x, ring_y,
						   mapx, mapy, b.anim_layer, b.opaque)) {
				return false;
			}
		}
		return true;
	}

private:
	eng::Span<const TileLayerBinding> m_layers {};
	eng::u8 m_baked_through = 0xffu;
};

} // namespace eng::playfield
