#pragma once

/// \file xlimited_scroll_layer.hpp
/// **Adapta una `XlimitedScene` a `ScrollLayer<Backend>`** (`eng::playfield::XlimitedScrollLayer`)
/// para que el `App` la arranque y conduce por el mismo contrato que el camino de tiras, **sin
/// `void*` ni punteros a función**. `Scene` es el tipo concreto de la escena (lleva su `Map` y su
/// `Profile`); `Backend`, el del `App`.
///
/// El juego declara el adaptador (típicamente como miembro, junto a la escena) y lo registra:
///
/// ```cpp
/// playfield::XlimitedScene<kScroll, MapView, Profile> scene;
/// playfield::XlimitedScrollLayer<decltype(scene), eng::amiga::AmigaBackend> layer {scene};
/// // en init(App&):
/// scene.set_config(config);
/// scene.track_camera(&cam_x, &cam_y);
/// app.add_scroll_layer(layer);
/// ```

#include <eng/field/scroll_layer.hpp>
#include <eng/field/scroll_plan.hpp>
#include <eng/field/xlimited_scene.hpp>
#include <eng/graphics/frame_plan.hpp>
#include <eng/memory/memory_manager.hpp>

namespace eng::playfield {

/// **Siembra la parte común** de una `XlimitedSceneConfigT` desde un `ScrollPlan` (geometría +
/// paleta). Lo **específico del corcóscru** (`map`/`map2`, `fg_row_fn`/`bg_row_fn`, DPF, HUD,
/// `path`) lo fija el juego aparte. Es el vocabulario común con el camino de tiras (§7(e) de
/// `ROADMAP_GAME_API`).
template <class MapT>
constexpr void apply_scroll_plan(XlimitedSceneConfigT<MapT>& cfg, const ScrollPlan& plan) noexcept {
	cfg.viewport_w = plan.viewport_w;
	cfg.viewport_h = plan.viewport_h;
	cfg.tile_width = plan.tile_w;
	cfg.tile_height = plan.tile_h;
	cfg.planes = plan.planes;
	if (plan.display_height != 0u) {
		cfg.display_height = plan.display_height;
	}
	if (plan.tilemap.palette.size() != 0u) {
		cfg.palette = plan.tilemap.palette;
	}
	cfg.parallax_plane = plan.parallax_plane; // RoboCod: plano de fondo con offset propio
	cfg.parallax_div = plan.parallax_div;
}

/// Adaptador `XlimitedScene` → `ScrollLayer<Backend>` (ver doc del fichero).
template <class Scene, class Backend>
class XlimitedScrollLayer final : public ScrollLayer<Backend> {
public:
	explicit XlimitedScrollLayer(Scene& scene) noexcept : m_scene(scene) {}

	/// Arranca la escena: `begin` (reserva en Chip) → cámara inicial → `fill` → `compose` → `takeover`.
	[[nodiscard]] bool begin(MemoryManager& memory, Backend& backend) noexcept override {
		if (!m_scene.begin(memory, m_scene.config())) return false;
		m_scene.bg().set_camera(0, 0); // posición inicial del scroll antes de pintar el anillo
		if (!m_scene.fill(backend, m_plan)) return false;
		if (!m_scene.compose()) return false;
		m_scene.takeover(backend);
		return true;
	}

	/// Conduce un frame: la cámara registrada en la escena → `update`+blit+`compose`+`install`.
	void frame(Backend& backend) noexcept override { m_scene.frame_from_source(backend); }

	/// Vistas de banda de la escena (1 = single, 2 = DPF): PF1 = `bg()` (delante), PF2 = `fg()`.
	[[nodiscard]] eng::u8 band_view_count() const noexcept override { return m_scene.fields(); }
	[[nodiscard]] PlayfieldHardwareView band_view(eng::u8 i) const noexcept override {
		return i == 0u ? m_scene.bg().hardware_view() : m_scene.fg().hardware_view();
	}

private:
	Scene& m_scene;
	graphics::FramePlan m_plan {};
};

} // namespace eng::playfield
